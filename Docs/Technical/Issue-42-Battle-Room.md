# #42 전투 방 구현

## Issue

- Issue 번호: #42
- 기능명: 전투 방 구현
- 담당자: dalbi090211
- 담당 영역: Sector Event / Battle Sector / Screen Text / Interact
- 관련 Sprint: 4주차


## 목적

1. 어떤 방은 제한 시간 내에 적들을 처치하며 목표에 도착해야 한다. 이 경우에는 목표로 가는 길에 몬스터들이 갑자기 스폰되어야 한다.
2. 어떤 방은 한 오브젝트를 지키며 몰려드는 적들을 처치해야 한다. 이 경우에는 특정 시간이 지날때마다 몬스터들이 스폰되어야 한다.
3. 어떤 방은 몬스터들을 전부 처치하기만 하면 된다. 이 경우에는 처음부터 씬에 배치되면 된다.

몬스터를 배치하는 것 이외에도 도전 성공 시 메시지를 띄우는 것, 보스 전투 진입 시의 컷신, 스테이지 클리어 후의 보상 상자 스폰 등 많은 관계들이 존재했고 이를 하나하나 정의하는 구조는 비효율적이라고 생각했다.

그래서 **트리거 조건**(언제)과 **일어날 일**(무엇을)을 분리했다. 트리거는 C++ 클래스가 갖고, 일어날 일은 레벨에서 편집하는 이벤트 목록으로 둔다.
현재 정의된 트리거 조건은 박스 오버랩, 스테이지 시작(beginplay), 상호작용 이고, 박스 오버랩을 통해서는 몬스터의 스폰, 스테이지 시작을 통해서는 스테이지 정보 ui에 표기를 구현하였다.

구현 범위는 다음과 같다.

- 섹터 이벤트 공통 인터페이스 (`ISectorEvent`)
- 시간 오프셋을 가진 이벤트 묶음 (`FTimedEventGroup`)
- 트리거별 배치형 이벤트 (볼륨 진입 / 레벨 진입 / 상호작용)
- 화면 텍스트 이벤트와 그 전달 경로 (`UEventManager`, `UScreenTextEvent`)
- 상호작용 안내 문구 (`IInteractable::GetInteractText`)

## 전체 구조

```text
ISectorEvent                        Execute(WorldContext) 하나만 가진 실행 인터페이스
 ├─ UConceptEvent (UDataAsset)      씬에 없는 이벤트. 에셋으로 만들어 여러 곳이 공유
 │   └─ UScreenTextEvent            화면 텍스트 출력
 └─ AScenePlacedEvent (AActor)      씬에 배치되는 이벤트. PreEvents / PostEvents 보유
     ├─ AConstBattleSector          배치된 몬스터를 스폰하고 전멸을 감시
     │   └─ AOverlapBattleSector    플레이어가 볼륨에 들어오면 시작
     ├─ ALevelTriggerEvent          레벨 진입(BeginPlay) 시 시작
     ├─ APlacedInteractActor        상호작용 키로 시작 (+ IInteractable)
     └─ ARewardEvent                보상 (필드만 있고 미구현)
```

전투 방 한 사이클:

```text
플레이어가 OverlapBox 진입
 └─ 서버 권한 확인 → 재진입 가드 → 플레이어 여부 확인
     └─ Execute(this)
         ├─ PreEvents 실행       (전투 시작 텍스트 등)
         └─ spawnMonster()       숨겨둔 몬스터 표시 (AI 정지·재개는 미구현, "외부 의존" 절 참고)

몬스터 사망 (OnMonsterDeath)
 └─ popMonster()
     └─ TryCheck() == TargetMonsters.IsEmpty()
         └─ PostEvents 실행      (다음 스테이지 시작 혹은 포탈 오픈 등 등록한 이벤트에 따른 유기적인 처리)
```

## 구현 파일

### 이벤트 공통

```text
Source/TeamProject_Fin/PCGMap/MapBuild/SectorBuild/Event/
 ├─ SectorEvent.h/.cpp                          인터페이스, FTimedEventGroup, AScenePlacedEvent
 ├─ EventManager.h/.cpp                         화면 텍스트 / 안내 문구 허브 (GameInstanceSubsystem)
 ├─ ConceptEvent/ScreenTextEvent.h/.cpp
 └─ PlacedEvent/
     ├─ LevelTriggerEvent.h/.cpp
     ├─ PlacedInteractActor.h/.cpp
     ├─ RewardEvent.h
     └─ BattleSector/
         ├─ ConstBattleSector.h/.cpp
         └─ OverlapBattleSector.h/.cpp
```

### 상호작용

```text
Source/TeamProject_Fin/GameCore/
 ├─ Interface/Interactable.h                    Execute(PC), GetInteractText()
 ├─ Interact/
 │   ├─ InteractComponent.h/.cpp                플레이어 부착. 후보 수집 + 실행 + 문구 갱신
 │   └─ InteractTriggerComponent.h/.cpp         상호작용 액터 부착. 등록/해제 배선
 ├─ Character/PlayerCharacter.cpp               InteractComponent 부착(:75), BindInput 호출(:338)
 └─ BaseGameState.h/.cpp                        화면 텍스트 멀티캐스트
```

## 이벤트 인터페이스

```cpp
class ISectorEvent
{
public:
    virtual void Execute(const UObject* WorldContext) {}
};
```

### WorldContext를 받는 이유

`UConceptEvent`는 `UDataAsset`이라 `GetWorld()`가 없다. 화면 텍스트·사운드·컷신 모두 월드나 GameInstance를 거쳐야 하므로 호출자를 컨텍스트로 넘긴다. `AScenePlacedEvent`는 자기 월드가 있어 기본값 `nullptr`로 호출할 수 있다.

## 시간 오프셋 이벤트 묶음

```cpp
USTRUCT(BlueprintType)
struct FTimedEventGroup
{
    UPROPERTY(EditInstanceOnly) float Delay = 0.f;
    UPROPERTY(EditInstanceOnly) TArray<TScriptInterface<ISectorEvent>> Events;
};
```

| 배열 | 기준 시점 | 용도 |
|---|---|---|
| `PreEvents` | `Execute()` 호출 시점 | 시작 텍스트, 문 잠금, 사운드 |
| `PostEvents` | 종료 조건 충족 시점 | 다음 웨이브, 보상, 클리어 텍스트 |

`PostEvents`를 언제 터뜨릴지는 베이스가 정하지 않고 파생이 정한다. 종료 조건이 클래스마다 다르기 때문이다(전투는 전멸, 보상은 획득, 사건 방은 선택 완료).

### 타이머 처리

```cpp
if (Group.Delay <= 0.f)         // 타이머를 거치지 않고 그 자리에서 실행
{
    ExecuteGroup(Group.Events);
    continue;
}

FTimerHandle Handle;            // 보관하지 않는다
GetWorldTimerManager().SetTimer(Handle,
    FTimerDelegate::CreateUObject(this, &AScenePlacedEvent::ExecuteGroup, Group.Events),
    Group.Delay, false);
```

`Delay`가 0 이하인 그룹은 호출한 프레임에 동기로 실행된다. 한 프레임 지연도 없어야 하는 시작 텍스트가 여기 해당한다.

핸들을 멤버로 들지 않는다. 액터가 파괴되면 TimerManager가 해당 객체에 바인딩된 타이머를 정리하므로 취소 경로가 필요 없다.

**역할이 끝난 섹터를 `Destroy()`하면 안 되는 이유도 여기에 있다.** 아직 발화하지 않은 `PostEvents` 그룹(예: 1.5초 뒤 다음 웨이브 스폰)이 함께 취소되어 진행이 조용히 끊긴다. 오버랩 비용이 신경 쓰이면 파괴 대신 `OverlapBox`의 콜리전만 끈다.

`ExecuteGroup`은 자기 자신이 목록에 있으면 건너뛴다. 배치 실수로 자기 자신을 지정하면 무한 재귀가 되기 때문이다.

## 트리거별 배치형 이벤트

### `AConstBattleSector`

레벨에 미리 배치해둔 몬스터를 `TargetMonsters`로 참조한다. 런타임 스폰이 아니라 배치 위치를 그대로 쓰기 때문에 레벨 디자인을 에디터에서 확인할 수 있다.

```text
BeginPlay()   TargetMonsters 전부 Hide()
Execute()     Super::Execute()로 PreEvents, 그다음 spawnMonster()
popMonster()  사망한 몬스터를 목록에서 제거, 비었으면 PostEvents
TryCheck()    TargetMonsters.IsEmpty()
```

### `AOverlapBattleSector`

`UBoxComponent` 오버랩으로 트리거한다.

- `HasAuthority()` — 스폰과 이벤트 체인 전체를 서버에서만 시작
- `bActivated` — 재진입 시 재스폰 방지
- `IsPlayerControlled()` — 몬스터가 자기 볼륨을 밟는 경우 제외

### `ALevelTriggerEvent`

`BeginPlay`에서 `Execute(this)`. 맵 이름/설명 같은 레벨 진입 연출용이다.

**권한 가드를 일부러 두지 않았다.** 레벨 배치 액터라 서버·클라 각자의 `BeginPlay`가 돈다. 각 머신이 로컬로 실행하면 `GameState` 멀티캐스트를 거치지 않아도 되고, 클라마다 로딩 완료 시점이 달라 멀티캐스트를 놓치는 문제도 없다.

대신 **이 액터의 `PreEvents`에는 서버 전용 이벤트(몬스터 스폰 등)를 물리면 안 된다.** 클라에서도 실행된다.

레벨 이동 시 텍스트를 띄우는 방법으로 `AMapTransObject`에서 트래블 전에 예약하는 안도 검토했으나 폐기했다. 트래블은 월드를 통째로 교체하므로 (1) 새 레벨의 WBP는 아직 없어 그 시점 Broadcast는 리스너가 없고, (2) `FTimerManager`는 World 소유라 예약 타이머가 소멸하며, (3) `GameState`도 새로 생성된다. 그리고 "레벨명 → 텍스트" 테이블을 별도로 관리하게 되어 레벨 추가 시 두 곳을 맞춰야 한다.

### `APlacedInteractActor`

`AScenePlacedEvent`와 `IInteractable`을 동시에 상속한다. 상호작용 키로 `PreEvents`가 발화한다. 탈출 시작, 상인, 보상 상자에 쓴다.

```cpp
//베이스 둘이 각각 Execute를 갖고 있어서 이름을 한 오버로드 집합으로 합친다
using AScenePlacedEvent::Execute;
virtual void Execute(APlayerController* PC) override;
```

`using`이 없으면 컴파일되지 않는다. C++ 이름 조회는 오버로드 결정보다 먼저 끝나고, 서로 다른 베이스에 있는 같은 이름은 자동으로 합쳐지지 않아 `Execute` 호출 자체가 모호해진다.

`bOnce`(기본 켜짐)를 끄면 상호작용할 때마다 다시 발화한다. 상자·탈출 장치는 1회성이고 상인만 반복이라 클래스를 나누는 대신 플래그로 처리했다.

메시는 C++에 넣지 않았다. 상인/상자/탈출 장치가 형태가 다 달라 BP에서 Add Component로 붙인다. 루트는 `UInteractTriggerComponent`다.

| 용도 | `bOnce` | `PreEvents` 예 |
|---|---|---|
| 탈출 시작 | 켜짐 | 텍스트 → 타이머 → 몬스터 웨이브 섹터 |
| 상인 | 꺼짐 | 상점 UI 열기 |
| 보상 상자 | 켜짐 | 보상 지급 → 획득 텍스트 |

## 화면 텍스트

### 전달 경로

```text
UScreenTextEvent::Execute(WorldContext)
 └─ ABaseGameState::MulticastScreenText   (NetMulticast, Reliable)
     └─ 각 클라이언트 UEventManager::ShowScreenText
         ├─ ActiveTexts에 저장
         ├─ OnScreenTextRequested.Broadcast(Text, Intensity)
         └─ Intensity별 타이머 시작
             └─ HideScreenText(Intensity)
                 ├─ ActiveTexts에서 제거
                 └─ OnScreenTextHideRequested.Broadcast(Intensity)
```

`UEventManager`는 `UGameInstanceSubsystem`이라 클라이언트마다 따로 존재한다. 이벤트 체인은 서버에서만 도는데(`HasAuthority` 가드) 서버의 서브시스템에 직접 넣으면 호스트 화면에만 뜬다. 그래서 모든 클라이언트에 항상 relevant한 `GameState`의 `NetMulticast`를 경유한다.

`FText`는 자체 NetSerialize를 가져 RPC 파라미터로 그대로 넘어간다.

멀티캐스트는 서버에서 호출될 때만 전파되고 클라에서 호출되면 로컬 실행된다. `ALevelTriggerEvent`가 이 폴백을 이용한다.

### 리스너보다 이벤트가 먼저 발생하는 문제

`ALevelTriggerEvent`를 처음 붙였을 때 텍스트가 전혀 표시되지 않았다. 원인은 타이밍이다.

HUD의 BaseLayer는 `ABasePlayerController::BeginPlay()`에서 생성된다(`BasePlayerController.cpp:30`). 레벨 배치 액터의 `BeginPlay`는 그보다 먼저 도는 경우가 많아, `Broadcast` 시점에 바인드된 리스너가 0명이었다. 델리게이트는 조용히 아무 일도 하지 않는다. 오버랩 트리거는 플레이어가 걸어 들어온 시점이라 위젯이 이미 붙어 있어 정상 동작했다.

지연으로 회피하면 로딩 시간에 따라 또 어긋난다. 문제는 "이벤트가 너무 이르다"가 아니라 "리스너가 아직 없다"이므로, `UEventManager`가 지금 떠 있어야 할 텍스트를 들고 있다가 늦게 바인드한 위젯에 넘겨주게 했다.

```cpp
TMap<EScreenIntensity, FText> ActiveTexts;   // ShowScreenText가 채우고 HideScreenText가 지운다
void ReplayActiveTexts();                    // WBP가 Construct에서 바인드 직후 호출
```

타이머는 원래 호출 시점부터 계속 돌기 때문에 위젯이 늦게 붙으면 남은 시간만 표시된다. 이미 만료된 텍스트는 `ActiveTexts`에 없어 뒤늦게 튀어나오지 않는다. 복구는 같은 레벨 안에서만이다. 레벨 트래블에서는 복구하지 않고 아래 절처럼 상태를 비운다.

### 레벨 트래블 시 상태 초기화

`UEventManager`는 `UGameInstanceSubsystem`이라 레벨이 바뀌어도 살아남는다. 이게 pawn 교체를 넘기는 장점이자 그대로 두면 버그다.

- 트래블은 월드를 통째로 버리므로 `UInteractTriggerComponent`의 `EndOverlap`이 오지 않는다 → `Unregister`가 안 돌고 `InteractPrompt`가 옛 레벨 값 그대로 남는다
- `HideScreenText` 타이머는 World 소유라 함께 죽는다 → 만료되지 못한 `ActiveTexts` 항목이 영구히 남는다

두 잔재가 새 레벨 위젯의 `ReplayActiveTexts`로 그대로 다시 뜬다. 그래서 서브시스템이 트래블 직전에 자기 상태를 비운다.

```cpp
void UEventManager::OnPreLoadMap(const FString&)   // Initialize에서 FCoreUObjectDelegates::PreLoadMap에 바인드
{
    HideTimers.Empty();
    ActiveTexts.Empty();
    InteractPrompt = FText::GetEmpty();
}
```

`PostLoadMapWithWorld`가 아니라 `PreLoadMap`이다. 후자는 새 월드의 `BeginPlay` 뒤에 오기 때문에, 트리거 안에서 스폰된 플레이어가 방금 등록한 문구까지 같이 지운다.

브로드캐스트는 하지 않는다. 옛 위젯은 어차피 파괴되고, 새 위젯은 `ReplayActiveTexts`로 빈 상태를 받아간다.

### Duration의 의미

타이머는 `ShowScreenText` 호출 즉시 시작한다. 따라서 `Duration`은 **페이드 인을 포함한 총 노출 시간**이며 페이드 인 길이보다 길어야 한다. 짧게 주면 페이드 인이 끝나기 전에 페이드 아웃이 시작되어 두 애니메이션이 같은 Render Opacity 트랙을 두고 경합한다(크래시는 없고 깜빡임과 팝이 발생).

페이드 인 종료 시점부터 재려면 WBP가 애니메이션 종료를 C++에 되돌려줘야 해서 왕복이 늘어난다. 값을 규율로 지키는 쪽을 택했다.

## 상호작용

### 대상 선택

```text
플레이어 상호작용 키
 └─ UInteractComponent::Invoke()
     └─ 권한 없으면 Server_Invoke()
         └─ 서버: GetNearest()로 자기 Candidates에서 선택
             └─ IInteractable::Execute(PC)
```

대상 선택은 서버가 자기 `Candidates`에서 한다. 클라이언트가 고른 대상을 서버가 믿으면 상호작용 대상을 클라가 지정하는 구조가 된다.

### 안내 문구

```cpp
// IInteractable
virtual FText GetInteractText() const { return FText::GetEmpty(); }
```

고정 문구는 `APlacedInteractActor::InteractText`(EditInstanceOnly)로 넣고, 값이 변하는 문구는 파생에서 `GetInteractText`를 오버라이드한다.

```cpp
// 가격이 오르는 상인 예시
FText AMerchantActor::GetInteractText() const
{
    return FText::Format(NSLOCTEXT("Interact", "BuyPart", "랜덤 파츠 구매 ({0}골드)"), Price);
}

void AMerchantActor::OnRep_Price()    // Price는 ReplicatedUsing
{
    Trigger->RefreshPrompt();
}
```

`Execute`는 서버에서만 돌아 가격을 올린다. `Execute` 직후 로컬에서 문구를 갱신하면 아직 옛 가격을 읽으므로, 복제가 도착하는 `OnRep`에서 갱신해야 한다. `UInteractTriggerComponent::RefreshPrompt()`가 자기 범위 안의 플레이어들을 찾아 각자의 `UInteractComponent`를 갱신한다.

## 외부 의존: 몬스터 등장/은신

`AConstBattleSector`는 `ISpawnable`의 `Spawn()` / `Hide()`만 호출하고 그 안의 구현에는 관여하지 않는다. `AMonsterCharacter` 쪽 구현은 **다른 작업자가 담당**한다.

```text
BeginPlay()     → Monster->Hide()
spawnMonster()  → Monster->Spawn()
```

## 블루프린트

### 화면 텍스트 / 안내 문구 WBP

`WBP_GamePlayBase`(BaseLayer)가 셋을 전부 받는다. 바인드 대상은 `Get Game Instance Subsystem (EventManager)` 하나다.

```text
Event Construct
 ├─ Bind Event to On Screen Text Requested        (Text, Intensity)
 ├─ Bind Event to On Screen Text Hide Requested   (Intensity)
 ├─ Bind Event to On Interact Prompt Changed      (Prompt)
 └─ Replay Active Texts
```

애니메이션은 `EScreenIntensity` 순서(`Title` / `SubTitle` / `Explain`)에 1:1로 대응한다.

| Intensity | 표시 | 숨김 |
|---|---|---|
| `Title` | `Screen1Show` | `Screen1Hide` |
| `SubTitle` | `Screen2Show` | `Screen2Hide` |
| `Explain` | `Screen3Show` | `Screen3Hide` |
| (안내 문구) | `InteractShow` | `InteractHIde` |

`OnScreenTextRequested`는 Intensity로 Switch → `SetText` → 해당 Show 재생, `OnScreenTextHideRequested`는 Switch → 해당 Hide 재생이다.

### 안내 문구 분기

```text
On Interact Prompt Changed (Prompt)
 └─ Text Is Empty?
     ├─ True  → Play Animation (InteractHIde)
     └─ False → Set Text (InteractTextBox)
                 └─ 지금 보이는 중?
                     ├─ Yes → 끝  (대상 교체)
                     └─ No  → Stop Animation (InteractHIde) → Play Animation (InteractShow)
```

- 빈 텍스트가 왔을 때 즉시 지우면 페이드 아웃 동안 빈 라벨이 사라지는 게 보인다. 지우려면 `InteractHIde` Finished에서.
- 이미 보이는 중에 `InteractShow`를 다시 재생하면 가격이 480→520으로 바뀔 때 문구가 깜빡인다.
- 빈 텍스트가 연속으로 두 번 오는 경우는 없다. `SetInteractPrompt`가 `EqualTo`로 걸러 실제로 달라졌을 때만 브로드캐스트한다.

### 레벨 배치

**전투 방**

1. 레벨에 몬스터를 배치한다.
2. `AOverlapBattleSector` 기반 액터를 배치하고 `OverlapBox` 크기를 조정한다.
3. `TargetMonsters`에 1번의 몬스터들을 등록한다.

**웨이브 연결** — 1번 웨이브의 `PostEvents`에

```text
Delay 0.0  → DA_Text_Stage2
Delay 1.5  → (배치한 2번 웨이브 ConstBattleSector 액터)
```

2번 웨이브의 `PostEvents`에 다시 다음 웨이브를 걸면 체인이 이어진다.

**레벨 진입 문구** — 각 섹터 레벨에 `ALevelTriggerEvent` 하나를 놓고 `PreEvents`에

```text
Delay 0.0  → DA_Text_RoomName   (Intensity: Title)
Delay 0.3  → DA_Text_RoomDesc   (Intensity: SubTitle)
```

**화면 텍스트 에셋** — `UScreenTextEvent` 데이터 애셋을 만들어 `Text` / `Intensity` / `Duration`을 채운다. 여러 섹터가 같은 에셋을 공유할 수 있다.

## 확인 항목

C++과 WBP는 붙었고, 아래는 리슨 서버 PIE에서 확인이 끝난 항목이다.

- [x] 볼륨 진입 시 배치 몬스터가 스폰됨
- [x] 리슨 서버에서 모든 클라이언트에 텍스트가 표시됨
- [x] `ALevelTriggerEvent`의 텍스트가 `ReplayActiveTexts` 경로로 표시됨
- [x] 상호작용 문구가 진입/이탈에 맞춰 표시·숨김
- [x] 호스트 화면에 다른 플레이어의 문구가 뜨지 않음
- [x] 트리거 안에서 포탈을 타고 나갔을 때 새 레벨에 옛 문구가 남지 않음

## 남은 작업

- `ARewardEvent` 실제 보상 로직 (`PosObjects` 필드만 있고 동작 없음)
- 상인 액터 (`APlacedInteractActor` 파생 + `Price` 복제 + `GetInteractText` 오버라이드)
- Intensity 추가(제목, 소제목, 경고 문구 와는 다른 ui 표기)


## 확장 방향

```text
ISectorEvent 구현 추가
 ├─ UConceptEvent 계열      사운드, 컷신, 카메라 셰이크, 문 잠금
 └─ AScenePlacedEvent 계열  상점 방(#43), 사건 방(#45), 웨이브 방어

전투 방 변형
 ├─ 시간 제한 전투          타이머 그룹으로 실패 이벤트 예약
 ├─ 웨이브 방어             PostEvents 체인 길이로 표현
 └─ 조건부 진행             종료 조건을 파생 클래스에서 교체
```
