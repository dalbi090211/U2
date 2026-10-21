# #24 폭파병 특이점 스킬 구현

## Issue

- Issue 번호: #24
- 기능명: 폭파병 특이점 스킬 구현
- 담당 영역: Ability / GAS / Animation / Projectile
- 관련 의존성: #4 GameplayTag 구조, #5 GameplayEffect 기본 구조, #22 폭파병 클래스

## 목적

스킬 키를 한 번 누르면 지뢰를 던지고, 같은 키를 한 번 더 누르면 던져 둔 지뢰를 터뜨린다. 정해진 시간 안에 누르지 않으면 지뢰가 스스로 폭발한다.

폭발은 데미지를 주고 시전자 본인을 밀어낸다. 때문에 일반 스테이지에서는 뭉쳐 있는 적을 터뜨리거나 기동성을 높이는 용도로, 보스 스테이지에서는 보스로부터 순간적으로 거리를 벌리거나 위험한 포지션을 벗어나는 등 다양한 용도로 사용할 수 있도록 설계하였다.

프로젝트는 소울류 전투를 지향하므로 이 반동은 공중 이동 수단이 아니라 적과의 거리를 벌리는 이탈 기동으로 사용하는 것을 기준으로 값을 잡았고, 너무 과도한 기동성을 제공하여 게임의 재미를 해치지 않도록 수치를 조정하였다.

## 주요 구현 내용

### 한 키로 투척과 폭파를 나누는 구조

스킬 키에 바인딩되는 어빌리티는 `UGA_BomberSkill_Mine` 하나이며, 두 번째 입력인지는 인스턴스 멤버가 아니라 `State.Skill.Bomber.MineArmed` 태그로 판단한다.

- 태그가 없으면 투척: 투척 몽타주를 재생하고, 손을 떠나는 프레임의 AnimNotify를 기다린다
- 태그가 있으면 폭파: `Event.Skill.Bomber.MineDetonate` 이벤트 발송, 기폭 몽타주 재생 후 즉시 `EndAbility`

폭파 쪽은 연출을 시작한 직후 `EndAbility`를 호출한다. 몽타주를 기다리지 않고 끝내야 `State.Skill.Active`가 풀려 다음 입력이 들어온다. 몽타주는 어빌리티가 끝나도 그대로 재생된다.

어빌리티가 `LocalPredicted`로 도는 이유는 시전자 본인 화면에도 몽타주가 보여야 하기 때문이다. `ServerOnly`로 두면 복제된 몽타주가 locally controlled 쪽에서 재생되지 않는다. 따라서 두 입력의 분기 기준도 서버와 클라이언트가 함께 보는 태그여야 한다.

### 투척 시점을 AnimNotify로 맞추기

지뢰가 실제로 스폰되는 시점은 스킬 입력 시점이 아니라 투척 몽타주에서 손을 떠나는 프레임이다. 입력과 동시에 스폰하면 캐릭터가 아직 팔을 들고 있는데 지뢰가 앞으로 날아간다.

몽타주의 해당 프레임에 기존 `UAN_SendGameplayEvent` 노티파이를 두고 `Event.Skill.Bomber.MineThrow`를 보내면, 어빌리티가 `UAbilityTask_WaitGameplayEvent`로 이를 받아 스폰, 지속시간 GE 부여, 기본공격 차단 해제, `EndAbility`를 한 번에 처리한다. 이 처리는 `ReleaseMine()` 한 곳에만 있다.

노티파이가 몽타주에 박혀 있지 않거나 몽타주가 중간에 끊겨도 지뢰는 나가야 하므로, 몽타주 길이만큼의 예비 타이머를 함께 걸고 둘 중 먼저 오는 쪽이 `ReleaseMine()`을 부른다. 먼저 온 쪽이 다른 쪽을 정리한다.

노티파이는 몽타주가 재생되는 곳마다 각자 발생한다. 어빌리티가 `LocalPredicted`라 서버와 소유 클라이언트가 각각 자기 시점에 이 흐름을 타며, 실제 스폰만 서버로 제한된다.

### 폭파 전용 어빌리티 분리

폭파는 `UGA_BomberSkill_MineDetonate`가 맡는다. 이 어빌리티는 `ServerOnly`이며 `Event.Skill.Bomber.MineDetonate` 게임플레이 이벤트로 트리거된다. 터뜨릴 지뢰는 이벤트 페이로드의 `Target`으로 들어온다.

입력 어빌리티와 분리한 이유는 예측 경로와 권위 경로를 섞지 않기 위해서다. 입력과 연출은 예측으로 돌고, 실제 피해와 액터 파괴는 서버에서만 일어난다.

### 지뢰 액터

`ABomberMine`은 `UProjectileMovementComponent`로 던져져 바운스하다가 마찰로 멈춘다. 스폰과 폭발은 서버에서만 처리하고 클라이언트는 액터 복제로 위치만 본다. 클라이언트가 예측으로 지뢰를 스폰하면 서버 것과 두 개가 보이므로 스폰은 서버로 한정했다.

던진 직후 시전자를 이동 무시 대상으로 등록해 자기 캡슐에 걸려 발밑에 떨어지는 것을 막는다.

`Detonate()`는 두 번 호출해도 한 번만 동작하며, 아래 순서로 처리한다.

1. `WeaponDamageLibrary::ApplyGasRadialDamage`로 반경 피해 적용 (적대 대상만)
2. 시전자 반동 적용 (반동 반경 안에 있을 때만)
3. `GameplayCue.Skill.Bomber.MineExplode` 큐 실행
4. `State.Skill.Bomber.MineArmed`를 부여한 GE 제거
5. 액터 파괴

지속시간 GE를 걷어내는 지점은 이 함수 한 곳뿐이다. 눌러서 터지든 신관 만료로 터지든 같은 경로를 타므로 태그가 남는 경우가 생기지 않는다.

### 신관과 지속시간 GE의 값 일치

지뢰가 스스로 터지기까지의 시간은 어빌리티의 `ArmedSeconds` 하나에서 나온다. 이 값을 지뢰의 신관 타이머에 넘기고, 동시에 GE 스펙의 Duration도 같은 값으로 덮어쓴다. GE 에셋에 적힌 Duration 값은 사용하지 않으므로 두 값이 어긋날 여지가 없다.

### 쿨다운 예외

`UGA_BomberSkill_Mine::CheckCooldown`은 `State.Skill.Bomber.MineArmed`가 있으면 항상 통과시킨다. 쿨다운은 투척 시점에 걸리는데, 이후 두 번째 입력이 쿨다운에 막히면 던져 둔 지뢰를 터뜨릴 수 없기 때문이다.

### 시전자 전용 폭발 반동

폭발 반경 안에 시전자가 있으면 폭심 반대 방향으로 밀려난다. 반동은 시전자에게만 적용되며 몬스터는 피해만 받는다.

수평 속도와 수직 속도를 따로 계산해 더한다. 방향 벡터에 위쪽 비중을 섞어 정규화하는 방식은 수평과 수직이 서로 힘을 나눠 갖기 때문에, 뜨는 높이를 줄이면 미는 거리도 같이 줄어든다. 두 축을 분리하면 이탈 거리를 유지한 채 높이만 낮출 수 있다.

```cpp
const float Falloff = FMath::Lerp(1.0f, KnockbackEdgeScale, Distance / KnockbackRadius);
const FVector HorizontalDirection = FVector(ToInstigator.X, ToInstigator.Y, 0.0f).GetSafeNormal();
const FVector LaunchVelocity =
    (HorizontalDirection * KnockbackStrength + FVector::UpVector * KnockbackUpSpeed) * Falloff;
```

거리 감쇠는 폭심에서 1, 반경 끝에서 `KnockbackEdgeScale`까지 선형으로 줄어든다. 위치에 따른 편차가 크면 어디서 터뜨릴지에 조작이 지나치게 민감해지므로, 끝에서 0이 되는 대신 최저 비율을 파라미터로 뒀다. 발밑에서 터져 수평 방향이 나오지 않으면 위쪽 성분만 적용된다.

네트워크 처리는 서버에서 `LaunchCharacter`를 호출하고, 이어서 시전자 클라이언트로 `Client` RPC를 보내 같은 값으로 한 번 더 호출한다. 서버 위치 보정만 기다리면 반동이 한 박자 늦게 들어온다. 지뢰의 Owner가 시전자이므로 RPC는 별도 처리 없이 시전자의 커넥션으로 간다. 리슨 서버 호스트에서는 두 호출이 겹치지만 `LaunchCharacter`는 대기 중인 속도를 누적하지 않고 덮어쓰므로 결과가 같다.

### 몽타주 재생 중 기본공격 차단

기본공격은 아래 구간에서 막힌다.

- 투척: 스킬 입력부터 지뢰가 손을 떠나는 노티파이까지. 던지고 난 뒤 남은 몽타주 구간은 막지 않는다
- 기폭: 기폭 몽타주 길이만큼

지뢰를 던지는 동작 중에 사격 몽타주가 겹쳐 상체가 끊기는 것을 막으면서, 던진 뒤에는 바로 사격으로 이어갈 수 있게 하기 위한 구분이다.

차단은 `State.Skill.Bomber.MineThrowing` 태그로 표시하고, `UGA_Fire`가 이 태그를 `ActivationBlockedTags`에 추가해 활성화를 막는다. 다만 활성화 차단만으로는 이미 발사 중인 어빌리티가 멈추지 않으므로, 몽타주를 시작할 때 `Ability.Weapon.Fire` 태그를 가진 어빌리티를 함께 취소한다.

태그는 어빌리티의 `ActivationOwnedTags`가 아니라 Loose Tag로 직접 건다. 기폭 쪽 어빌리티는 몽타주를 시작한 직후 끝나므로 `ActivationOwnedTags`로는 구간이 잡히지 않고, 투척 쪽은 어빌리티가 살아 있지만 두 경로가 같은 방식으로 걸리는 편이 추적하기 쉽다. 태그를 거는 곳은 `PlaySkillMontage()`, 거두는 곳은 투척이면 `ReleaseMine()`, 기폭이면 몽타주 길이 타이머다.

투척과 기폭 몽타주가 짧은 간격으로 이어져도 태그 카운트가 남지 않도록 개수를 더하지 않고 `SetLooseGameplayTagCount`로 0 또는 1을 지정한다.

기존 `State.Skill.Active`를 그대로 쓰지 않은 이유는 그 태그가 모든 캐릭터 스킬에 공통으로 붙기 때문이다. 사격 차단이 폭파병 지뢰뿐 아니라 전 클래스 스킬에 걸리게 되므로 지뢰 전용 태그를 따로 뒀다.

Loose Tag는 복제되지 않지만 어빌리티가 `LocalPredicted`라 몽타주 재생 경로가 서버와 소유 클라이언트 양쪽에서 실행되고, 사격 어빌리티의 활성화 판정도 같은 두 곳에서 일어나므로 문제가 없다.

## 관련 구현

- C++
  - `Source/TeamProject_Fin/GameCore/Ability/BomberMine.h`
  - `Source/TeamProject_Fin/GameCore/Ability/BomberMine.cpp`
  - `Source/TeamProject_Fin/GameCore/Ability/GA_BomberSkill_Mine.h`
  - `Source/TeamProject_Fin/GameCore/Ability/GA_BomberSkill_Mine.cpp`
  - `Source/TeamProject_Fin/GameCore/Ability/GA_BomberSkill_MineDetonate.h`
  - `Source/TeamProject_Fin/GameCore/Ability/GA_BomberSkill_MineDetonate.cpp`
  - `Source/TeamProject_Fin/GameCore/Ability/GA_Fire.cpp`
  - `Source/TeamProject_Fin/GameCore/Animation/AN_SendGameplayEvent.h` (기존 노티파이 재사용)
  - `Source/TeamProject_Fin/GameCore/Tags/GameplayTagSystem.h`
  - `Source/TeamProject_Fin/GameCore/Tags/GameplayTagSystem.cpp`
- Blueprint
  - `Content/TPSDevelop/Class/Skill/BP_LandMine`
  - `Content/TPSDevelop/Class/Skill/BP_GA_Bomber_Landmine`
  - `Content/TPSDevelop/Class/Skill/BP_GA_Bomber_MineDetonate`
- Asset
  - `Content/TPSDevelop/Class/Skill/GE_MineArmed`
  - `Content/GameplayCues/GC_Skill_Bomber_MineExplode`
- GameplayTag
  - `State.Skill.Bomber.MineArmed`
  - `State.Skill.Bomber.MineThrowing`
  - `Event.Skill.Bomber.MineDetonate`
  - `Event.Skill.Bomber.MineThrow`
  - `GameplayCue.Skill.Bomber.MineExplode`

## 사용 방법

### 1. 지뢰 Blueprint 설정

`BP_LandMine`의 Class Defaults에서 아래 값을 설정한다.

| 항목 | 카테고리 | 기본값 | 설명 |
|---|---|---|---|
| `ExplosionRadius` | Mine \| Explosion | 400 | 피해 반경 |
| `ExplosionDamage` | Mine \| Explosion | 150 | 폭발 피해량 |
| `DamageEffectClass` | Mine \| Explosion | 없음 | 피해 GE. 비우면 피해가 들어가지 않는다 |
| `ExplosionCueData` | Mine \| Explosion | 없음 | 폭발 Niagara/사운드. 비우면 큐 에셋 기본값 사용 |
| `KnockbackRadius` | Mine \| Knockback | 450 | 반동이 들어오는 반경 |
| `KnockbackStrength` | Mine \| Knockback | 1300 | 폭심에서의 수평 속도. 0이면 반동 없음 |
| `KnockbackEdgeScale` | Mine \| Knockback | 0.7 | 반경 끝에서 받는 힘의 비율. 1이면 거리와 무관하게 동일 |
| `KnockbackUpSpeed` | Mine \| Knockback | 350 | 수평과 별개로 더해지는 위쪽 속도 |

반동 값을 잡는 기준은 아래와 같다.

- 이탈 거리는 `KnockbackStrength`로 잡는다.
- 뜨는 높이는 `KnockbackUpSpeed`로만 조절한다. 0으로 두면 지면 마찰에 막혀 거의 밀리지 않으므로 200~400 사이를 사용한다.
- 어디서 터뜨리든 비슷한 감각을 원하면 `KnockbackEdgeScale`을 1에 가깝게 올린다.

### 2. 스킬 어빌리티 Blueprint 설정

`BP_GA_Bomber_Landmine`의 Class Defaults에서 설정한다.

1. `MineClass`에 `BP_LandMine` 지정
2. `ArmedEffectClass`에 `GE_MineArmed` 지정
3. `ArmedSeconds`로 자동 폭발까지의 시간 조정 (기본 20초)
4. `ThrowSpeed`, `ThrowPitchOffset`, `ThrowForwardOffset`, `ThrowHeightOffset`으로 투척 궤적 조정
5. `ThrowMontage`, `DetonateMontage`, `MontagePlayRate` 지정
6. `CooldownGameplayEffectClass`와 `CooldownDuration` 지정

`GE_MineArmed`는 Has Duration GE이며 `State.Skill.Duration`과 `State.Skill.Bomber.MineArmed`를 부여한다. Duration 값은 `ArmedSeconds`로 덮어쓰므로 에셋 값은 사용하지 않는다.

### 3. 투척 몽타주에 노티파이 배치

`ThrowMontage`에서 지뢰가 손을 떠나는 프레임에 `Send Gameplay Event` 노티파이(`UAN_SendGameplayEvent`)를 추가하고 `EventTag`에 `Event.Skill.Bomber.MineThrow`를 지정한다.

노티파이를 두지 않으면 몽타주가 끝나는 시점에 지뢰가 나간다. 동작은 하지만 타이밍이 늦으므로 반드시 배치한다.

### 4. 폭파 어빌리티 부여

`BP_GA_Bomber_MineDetonate`는 폭파병 캐릭터에 함께 부여한다. 입력 바인딩은 필요 없으며 게임플레이 이벤트로 트리거된다.

## 테스트 및 확인 내용

- [x] 에디터 빌드 확인 (`TeamProject_FinEditor Win64 Development`)
- [ ] 투척 → 원격 폭파 → 자동 폭발(신관 만료)의 세 경로가 모두 동작하는지 확인
- [ ] 지뢰가 손을 떠나는 프레임에 맞춰 스폰되고, 그 직후부터 기본공격이 다시 나가는지 확인
- [ ] 반경 안의 적에게만 피해가 들어가고, 반동은 시전자에게만 들어가는지 확인
- [ ] 리슨 서버와 클라이언트 양쪽에서 지뢰 위치, 폭발 이펙트, 반동이 지연 없이 동일하게 보이는지 확인

## 확장 방향

- 지뢰 동시 저장 가능 (현재는 마지막에 던진 지뢰 하나만 추적한다)
- 스킬 시전 중 사격 차단을 전 클래스 공통 규칙으로 설정하여 `State.Skill.Bomber.MineThrowing`을 지우고 `State.Skill.Active`로 통합 (현재는 강화류 스킬에 시전 모션이 없어 지뢰 전용 태그로 분리해 두었다)
