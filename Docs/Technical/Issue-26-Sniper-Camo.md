# #26 스나이퍼 적 투시 스킬 구현

## Issue

- Issue 번호: #26
- 기능명: 스나이퍼 적 투시 스킬 구현
- 담당자: dalbi090211
- 담당 영역: Character Skill / GAS
- 관련 의존성: #21 `UGA_CharacterSkillBase`, #17 발사 시스템, #13 데미지 시스템, #31 AI Controller 및 State Tree

## 목적

스나이퍼의 고유 스킬. 시전하면 은신에 들어가고, 한 발 쏘면 은신이 풀렸다가 2초 뒤 다시 들어간다. 은신 중 쏘는 한 발은 강화된다.

은신은 20초 장막 안에서 토글된다. 상태가 계속 변화된다.
은신 연출 중 포스트 프로세스는 자기 화면에만, 머테리얼 교체는 GameplayCue(복제되는경로)를 통해 교체한다.

## 구현 파일

```text
Source/TeamProject_Fin/GameCore/Ability/
 ├─ GA_CharacterSkillBase.h/.cpp                 (#21에서 구현됨. 태그/쿨다운 정책)
 ├─ GA_SniperSkill_Camo.h/.cpp                   장막·은신 상태 전이
 └─ GameplayCues/GCN_StealthMaterial.h/.cpp      머티리얼 교체 + 포스트프로세스 블렌드

Content/
 ├─ TPSDevelop/Class/Skill/BPGA_Sniper_Cammo     GA BP
 ├─ Damage/GE_EnpowerEffect                      AttackPower 배수 + Cue 태그 보유
 └─ GameplayCues/GameplayCue_Skill_Camo_Stealth  Cue BP (GCN_StealthMaterial 파생)
```

## 발동 경로

```text
UniqueSkillAction (IA)
 └─ APlayerCharacter::StartUniqueSkill
     └─ TryActivateAbilitiesByTag(Ability.Skill.Main)
         └─ UGA_SniperSkill_Camo::ActivateAbility          ServerOnly
             ├─ WaitGameplayEvent(Event.Weapon.Fired)      OnlyTriggerOnce = false
             ├─ VeilTimer 예약                              VeilDuration 20초
             └─ EnterStealth()                             시전 직후 즉시 은신
```

`UGA_CharacterSkillBase`가 생성자에서 `Ability.Skill.Main`을 에셋 태그로 붙이므로 캐릭터 BP의 `DefaultAbilities`에 등록만 하면 된다.

`NetExecutionPolicy`는 베이스의 `LocalPredicted`를 **`ServerOnly`로 덮었다.** 상태 관리와 GE 적용이 전부 서버 몫이라 예측할 것이 없다.

## 상태 전이

장막(Veil) 20초 안에서 은신(Stealth)이 붙었다 떨어졌다 한다.

```text
시전 ──────────────────────────────────────────────── 20초 ──▶ 종료
  │                                                              │
  ├ Veil ON  ────────────────────── 계속 유지 ──────────────────┤
  └ Stealth ON ─┬─ 발사 → OFF ─ 2초 ─→ ON ─┬─ 발사 → OFF ─ …    OFF
                └ 재진입 타이머는 발사할 때마다 처음부터 다시 시작
```

| 시점 | Veil | Stealth | 트리거 |
|---|---|---|---|
| 스킬 시전 | ON | ON | `ActivateAbility` → `EnterStealth()` |
| 발사 | ON | **OFF** | `Event.Weapon.Fired` → `OnWeaponFired()` → `ExitStealth()` |
| 마지막 발사 후 2초 무공격 | ON | ON | `StealthTimer` → `EnterStealth()` |
| 시전 후 20초 | OFF | OFF | `VeilTimer` → `OnVeilExpired()` → `EndAbility` |
| 사망 / 취소 | OFF | OFF | `EndAbility`가 `ExitStealth()`를 먼저 부른다 |

`EnterStealth()`는 `EmpowerHandle` 유효성으로 가드되어 있어 중복 진입이 없다.

## 태그를 둘로 나눈 이유

`GameplayTagSystem.cpp:19-20`

| 태그 | 수명 | 의미 |
|---|---|---|
| `State.Skill.Camo.Veil` | 시전 ~ 20초, **연속** | 스킬이 도는 중. `ActivationOwnedTags` + `ActivationBlockedTags`로 재시전을 막는다 |
| `State.Skill.Camo.Stealth` | 장막 안에서 **토글** | 지금 실제로 안 보이는 상태 |

하나로 합치면 재시전 차단과 은신 판정이 같은 수명을 갖게 된다. 총을 쏴서 은신이 풀린 순간에도 스킬은 계속 돌아야 하므로 분리했다.

## AI 감지 제외 (#31 영역)

> AI는 대상 ASC에 `State.Skill.Camo.Stealth`가 있으면 감지·추적 대상에서 제외되어야 한다.

## 데미지 강화

은신 중에만 `GE_EnpowerEffect`(Infinite)가 붙어 `AttackPower`를 곱연산으로 올린다. `AttackPower` 기본값은 1.0(`CharacterAttributeSet.cpp:22`)이고, 데미지는 `UExecCalc_Damage`가 이 값을 Source에서 캡처해 곱한다(`ExecCalc_Damage.cpp:84`).

```text
FinalDamage = BaseDamage × AttackPower × (약점/팀킬 배수) × 방어력 경감
```

강화가 그 한 발에 적용되는 것은 **`GA_Fire`의 호출 순서가 보장한다.**

```text
UGA_Fire::TryFireOnce
 ├─ WeaponComponent->ExecuteFire(...)          GA_Fire.cpp:85   ← 데미지 적용
 └─ ASC->HandleGameplayEvent(Event.Weapon.Fired)  GA_Fire.cpp:125 ← 은신 해제
```

이벤트가 데미지보다 뒤에 있으므로, `ExitStealth()`가 GE를 떼는 시점에는 이미 강화된 데미지가 들어간 뒤다. 

> 히트 스캔이 아닌 라이플, 유탄의 경우 GE가 떼진 후에 데미지가 들어가기에 처리되지 않는다. 스나이퍼는 히트 스캔 방식을 사용해서 이 방식으로 구현했다.

## 연출 (`AGCN_StealthMaterial`)

`GE_EnpowerEffect`가 보유한 `GameplayCue.Skill.Camo.Stealth` 태그로 GE가 붙고 떨어질 때 자동으로 `OnActive` / `OnRemove`가 돈다.

| 요소 | 적용 범위 | 이유 |
|---|---|---|
| 머티리얼 교체 | **모든 머신** | 다른 플레이어에게도 반투명하게 보여야 한다 |
| 포스트프로세스 | **로컬 플레이어만** | `FindLocalCamera`가 `IsLocallyControlled`로 거른다 |

### 교체 대상 메시

`CollectStealthMeshes`가 캐릭터 메시와 **그 아래 붙은 모든 `UMeshComponent`**를 모은다. 무기는 별개 액터지만 `UWeaponComponent::EquipWeapon`이 캐릭터 메시 소켓에 붙이므로(`WeaponComponent.cpp:156`) 자식 컴포넌트로 딸려온다. 총과 총열이 같이 투명해진다.

조준선 프리뷰(`WeaponTrajectoryPreviewComponent`)는 캐릭터 메시가 아니라 루트 캡슐에 붙어서 걸리지 않는다. 의도한 동작이다.

### 머티리얼 함정

은신 머티리얼에 **Used with Skeletal Mesh** 플래그가 없으면 렌더러가 조용히 엔진 기본 머티리얼로 대체한다. 캐릭터가 반투명해지는 대신 머티리얼이 벗겨진 것처럼 보인다.

`ApplyStealth`가 `CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh)`로 먼저 확인하고, 통과하지 못하면 교체를 건너뛴다. 에디터에서는 이 호출 자체가 플래그를 켜고 셰이더를 재컴파일하므로 **머티리얼 에셋을 저장해야 유지된다.**

원본 머티리얼은 `FStealthMeshMaterials` 배열에 `UPROPERTY`로 들고 있다가 해제 시 슬롯별로 되돌린다. `UPROPERTY`가 없으면 은신 도중 원본이 GC로 날아가 복구가 깨진다.

### 블렌드

포스트프로세스 가중치는 0에서 시작해 `Tick`이 smoothstep으로 올린다. 목표 가중치를 바로 넣으면 한 프레임 만에 튄다.

- `BlendInTime` 0.35초 / `BlendOutTime` 0.12초
- 블렌드 아웃 도중 재진입하면 원복하지 않고 현재 가중치에서 방향만 뒤집는다
- 원복과 `GameplayCueFinishedCallback`은 블렌드가 끝난 뒤에 부른다. 그 전에 부르면 액터가 회수된다
- `Recycle()`에서도 원복한다. Cue 액터는 풀에서 재사용되므로 캐시를 비우지 않으면 남의 원본 머티리얼로 복구된다

## 블루프린트 작업

### `BPGA_Sniper_Cammo` (`UGA_SniperSkill_Camo` 기반)

| 프로퍼티 | 내용 |
|---|---|
| `EmpowerEffectClass` | `GE_EnpowerEffect`. **Infinite** 정책이어야 한다. Instant면 핸들이 남지 않아 해제가 안 된다 |
| `VeilDuration` | 장막 지속. 기본 20초 |
| `StealthDelay` | 재진입 대기. 기본 2초 |
| `CooldownGameplayEffectClass` / `CooldownDuration` | 베이스가 `ApplyCooldown`에서 사용. 비우면 연타된다 |

### `GE_EnpowerEffect`

| 항목 | 값 |
|---|---|
| Duration Policy | `Infinite` |
| Modifier Attribute | `CharacterAttributeSet.AttackPower` |
| Modifier Op | `MultiplyCompound` |
| Magnitude | 원하는 배수. **1.0이면 강화가 안 걸린다** |
| Gameplay Cues | `GameplayCue.Skill.Camo.Stealth` |

### `GameplayCue_Skill_Camo_Stealth` (`AGCN_StealthMaterial` 기반)

| 프로퍼티 | 내용 |
|---|---|
| `Stealth Material` | 반투명 머티리얼. Blend Mode `Translucent`, Shading Model `Unlit`, **Used with Skeletal Mesh 체크 필수** |
| `Stealth Post Process` | 피사계 심도(주변 흐리게) + 배경 어둡고 파랗게 |
| `Post Process Blend Weight` | 0이면 포스트프로세스가 안 보인다 |
| `Blend In Time` / `Blend Out Time` | 0.35 / 0.12 |

## 남은 작업

- 은신 중 무기 교체 대응. 새로 장착한 무기는 원래 머티리얼로 나온다. `#30` 드랍/교체가 들어온 뒤 구현
- 은신 중 이동속도 증가
- 은신 전환 시의 사운드 추가
- 은신 중 먹먹한 느낌의 사운드 필터 적용
- **(AI 작업자)** 추적 중인 몬스터가 대상이 은신하면 타깃을 놓음
- **(AI 작업자)** 은신 중 몬스터가 타깃을 잡지 않음