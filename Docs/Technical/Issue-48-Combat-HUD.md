# #48 전투 HUD 구현

## Issue

- Issue 번호: #48
- 기능명: 전투 HUD 구현
- 담당자: dalbi090211
- 담당 영역: UI
- 관련 Sprint: 6주차

## 목적

전투 HUD는 창 하나를 띄우는 것으로 끝나지 않는다. 창을 만들고, 이미 떠 있으면 재활용하고, ESC로 닫고, 뒤에 있던 창으로 돌아가는 처리가 필요하다. 위젯마다 `CreateWidget` + `AddToViewport`를 직접 하면 이 처리를 창 개수만큼 다시 만들게 된다.

그래서 창을 여닫는 규칙을 한 곳으로 모은다. 전투 HUD는 이 규칙의 첫 사용처이며, 진행 UI(#49)와 설정/옵션 UI(#50)도 같은 스택 위에 올라간다.

이번 단계의 구현 범위는 다음과 같다.

- 레이어 위젯 계층과 생성 / 재활용
- ESC 뒤로가기와 스택
- 컨트롤러를 통한 창 열기 경로
- `ABasePlayerState` 기반 체력 사본과 팀원 체력 표시

## 주요 구조

```text
ULayerWidgetBase        LayerName + Show() / Hide()
 ├─ UDefaultWidgetLayer  위에 쌓이는 창. ESC로 pop 된다
 └─ UBaseWidgetLayer     씬당 하나. 스택과 재활용을 전담한다

ABasePlayerController
 └─ UBaseWidgetLayer BaseLayer     BeginPlay에서 생성, 뷰포트 최하단
     └─ UDefaultWidgetLayer[]      그 위에 쌓이는 창들
```

`ULayerWidgetBase`가 공통으로 갖는 것은 `LayerName`과 표시 전환뿐이다.

```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) ELayerName LayerName;
void Show() { SetVisibility(ESlateVisibility::Visible); }
void Hide() { SetVisibility(ESlateVisibility::Collapsed); }
```

`LayerName`은 각 WBP가 직접 지정한다. `UBaseWidgetLayer`는 `WidgetClasses` 배열의 CDO에서 이 값을 읽어 어떤 창인지 판별하므로, **클래스와 이름을 따로 매핑하는 표를 두지 않는다.** 표를 두면 WBP를 추가할 때마다 두 군데를 고쳐야 하고 한쪽만 고치는 사고가 난다.

## 구현 파일

- `Source/TeamProject_Fin/GameCore/UI/LayerWidgetBase.h/.cpp`
- `Source/TeamProject_Fin/GameCore/UI/DefaultWidgetLayer.h/.cpp`
- `Source/TeamProject_Fin/GameCore/UI/BaseWidgetLayer.h/.cpp`
- `Source/TeamProject_Fin/GameCore/UI/UIStruct.h`
- `Source/TeamProject_Fin/GameCore/BasePlayerController.h/.cpp`
- `Source/TeamProject_Fin/GameCore/BasePlayerState.h/.cpp`
- `Source/TeamProject_Fin/GameCore/BaseGameState.h/.cpp`
- `Source/TeamProject_Fin/GameCore/Character/BaseCharacter.h/.cpp`
- `Source/TeamProject_Fin/GameCore/Character/PlayerCharacter.h/.cpp`

## 생성과 재활용

```cpp
UDefaultWidgetLayer* UBaseWidgetLayer::ShowWidget(ELayerName Name);
```

`Widgets`(`TMap<ELayerName, UDefaultWidgetLayer*>`)에 이미 있으면 그것을 다시 보이게 하고, 없을 때만 만든다. 닫을 때 파괴하지 않고 `Collapsed`로만 두므로 두 번째부터는 생성 비용이 없다. 옵션 창처럼 자주 여닫는 창에서 이 차이가 크다.

`Stack`은 열린 순서를 들고 있다. 이미 떠 있던 창을 다시 열면 스택에서 빼고 맨 위로 올린다.

ZOrder는 생성 시 한 번만 준다(`Stack.Num() + 1`). 재활용된 창이 항상 최상단이어야 하는 상황이 생기면 그때 `AddToViewport`를 다시 호출하도록 바꾼다.

## ESC 처리

```text
ESC 입력 → UBaseWidgetLayer::NativeOnKeyDown
    스택에 창이 있으면  → pop 후 Hide()
    스택이 비어 있으면  → BackWidgetName 창을 연다
```

`BackWidgetName`은 `EditDefaultsOnly`라 씬마다 다르게 지정한다. 로비 베이스는 게임 종료 확인 창, 게임플레이 베이스는 옵션 창을 가리킨다. 즉 **게임 중 ESC를 누르면 옵션이 뜨는 동작이 이 값 하나로 정해진다.**

ESC를 받으려면 입력 모드가 `Game and UI`이고 키보드 포커스가 베이스 레이어에 있어야 한다. `FocusSelf()`가 창을 열고 닫을 때마다 이를 다시 맞춘다.

베이스 레이어는 화면 전체를 덮으므로 `NativeConstruct`에서 `SelfHitTestInvisible`로 바꾼다. 자기 자신은 클릭을 먹지 않고 자식만 반응한다.

## 창을 여는 경로

창은 반드시 컨트롤러(또는 창 자신이 제공하는 함수)를 거쳐 연다. 직접 `CreateWidget`을 하면 스택에 등록되지 않아 ESC로 닫히지 않고 재활용도 되지 않는다.

```cpp
// PlayerController에서
ABasePlayerController::ShowWidget(ELayerName);
ABasePlayerController::CloseWidget();

// 창 안의 버튼에서 (UDefaultWidgetLayer가 제공)
ShowWidget(ELayerName::Option);   // 다른 창 열기
CloseWidget();                    // 자기 자신 닫기. ESC와 같은 동작
```

베이스 레이어는 `ABasePlayerController::BeginPlay`에서 `BaseLayerClass`를 보고 만든다. 로컬 컨트롤러에서만 만들며, 씬마다 다른 WBP를 지정한다.

## 레이어 이름

```cpp
UENUM(BlueprintType)
enum class ELayerName : uint8
{
    Option, QuitConfirm,                        // 공통
    LobbyMain, FindGame, MatchRoom, MakeRoom,   // 로비
    Main,                                       // 게임플레이
};
```

같은 이름의 다른 구현이 존재할 수 있다. 예를 들어 로비의 옵션Layer와 게임플레이의 옵션Layer가 같은 ELayerName을 지니며 다른 구현을 가질 수 있다. 이때 PlayerController에는 동일한 이름으로 등록된다.

## 체력 데이터의 소유자 — ABasePlayerState

### 왜 폰이 아니라 PlayerState인가

자기 체력만이라면 폰의 `UCharacterAttributeSet`을 직접 읽으면 된다. 문제는 팀원 체력이다.

폰은 넷 렐러번시 대상이다. 팀원이 멀리 떨어지거나 벽 뒤로 가면 그 폰의 액터 채널이 닫히고 클라이언트에서 사라진다. 그러면 체력바가 값을 잃는다. 반면 **`APlayerState`는 접속해 있는 한 모든 클라이언트에 항상 복제된다.** 그래서 표시용 체력 사본을 `ABasePlayerState`에 둔다.

원본은 여전히 서버의 어트리뷰트 하나뿐이고, PlayerState의 값은 UI 표시용 사본이다. 게임 로직은 이 값을 읽지 않는다.

### 구조

```cpp
USTRUCT(BlueprintType)
struct FDLPlayerVitals
{
    UPROPERTY(BlueprintReadOnly) float Health = 0.0f;
    UPROPERTY(BlueprintReadOnly) float MaxHealth = 0.0f;
};

// ABasePlayerState
UPROPERTY(ReplicatedUsing = OnRep_Vitals) FDLPlayerVitals Vitals;
UPROPERTY(BlueprintAssignable) FOnPlayerVitalsChanged OnVitalsChanged;  // (Health, MaxHealth)
```

Health와 MaxHealth를 각각 복제 프로퍼티로 두지 않고 **구조체 하나로 묶었다.** 따로 두면 두 값이 같은 프레임에 바뀔 때 OnRep이 두 번 돌고, 그사이에 `Health > MaxHealth`인 중간 상태가 UI에 한 프레임 노출된다.

같은 이유로 로드아웃(`FDLPlayerLoadout`)도 구조체 단위로 복제한다. 로드아웃 쪽은 [#47 서버 통합 및 Replication](https://github.com/2021Cyan/TeamProject3/blob/265059d8a3305a45c8f6c890590f4074069a3a88/Docs/Wiki/Issue-47-Server-Session-Replication.md)에서 다룬다.

### 값이 흘러가는 경로

```text
[서버] GE_InitAttributes / 데미지
    → UCharacterAttributeSet::Health 변경
    → APlayerCharacter::OnVitalsAttributeChanged
    → PushVitalsToPlayerState()          HasAuthority() 가드
    → ABasePlayerState::SetVitals()
         ├─ [서버] OnVitalsChanged 직접 Broadcast
         └─ 복제 → [클라] OnRep_Vitals → OnVitalsChanged Broadcast
```

`SetVitals`가 서버에서 델리게이트를 **직접** 쏘는 이유는 서버에서는 OnRep이 돌지 않기 때문이다. 리슨 서버의 호스트 화면도 클라이언트와 똑같이 갱신되어야 한다.

`PushVitalsToPlayerState`는 `BeginPlay`와 `PossessedBy` 양쪽에서 부른다. 스폰과 빙의 순서가 상황에 따라 뒤바뀌는데, 어느 쪽이 먼저든 한 번은 채워지게 하기 위해서다. `SetVitals`는 값이 같으면 조기 반환하므로 중복 호출 비용이 없다.

### 첫 값을 놓치는 문제

**이벤트만 바인딩하면 클라이언트는 첫 값을 못 받는다.** 어트리뷰트는 액터 스폰 번치에 실려 오는 경우가 많아, 클라이언트의 `BeginPlay`가 돌기 전에 이미 OnRep이 지나가 있다. 그러면 델리게이트를 붙인 시점 이후로 값이 한 번도 바뀌지 않고, 체력바는 위젯 기본값에 멈춘다. 서버에서는 바인딩이 먼저고 초기화 GE 적용이 나중이라 이 문제가 드러나지 않는다.

두 곳에서 막는다.

1. `ABaseCharacter::InitializeAbilitySystem` 끝에서 바인딩 직후 현재값으로 `OnHealthChanged` / `OnStaminaChanged`를 한 번 브로드캐스트한다. `ApplyMovementSpeed`가 같은 자리에서 하던 처리를 체력과 스태미나로 넓힌 것이다.
2. 위젯도 **바인딩한 다음 현재값을 한 번 직접 읽는다.** 위젯 생성 시점이 폰의 `BeginPlay`보다 늦으면 1번의 브로드캐스트도 이미 지나간 뒤이기 때문이다.

```text
Event Construct
  → Bind Event to OnVitalsChanged (또는 OnHealthChanged)
  → Get Health / Get Max Health 로 바를 한 번 갱신     ← 이 줄이 없으면 클라만 안 나온다
```

### 트래블 시 처리

`ABasePlayerState::CopyProperties`는 `Loadout`만 넘기고 `Vitals`는 넘기지 않는다. 심리스 트래블로 새 맵에 들어가면 폰이 새로 스폰되고 초기화 GE가 다시 적용되므로, 이전 맵의 체력을 들고 갈 이유가 없다. 값은 새 맵의 `PushVitalsToPlayerState`가 다시 채운다.

### 읽기용 API

```cpp
// 전부 BlueprintPure
float GetHealth() const;
float GetMaxHealth() const;
float GetHealthPercent() const;   // MaxHealth가 0이면 0. 프로그레스 바에 그대로 꽂는다
```

`GetHealthPercent`가 0으로 나누기를 막고 있으므로 위젯에서 따로 검사하지 않는다.

## 사용 방법

1. `UBaseWidgetLayer`를 상속한 WBP를 씬마다 하나 만든다 (로비용, 게임플레이용).
2. 그 WBP의 `WidgetClasses`에 해당 씬에서 열 창들을 등록하고, `BackWidgetName`을 지정한다.
3. 각 창 WBP는 `UDefaultWidgetLayer`를 상속하고 `LayerName`을 지정한다.
4. PlayerController BP의 `BaseLayerClass`에 1번의 WBP를 지정한다.
5. 레벨의 GameMode Override에서 해당 PlayerController BP를 사용하게 한다.
6. 전투 HUD(`WBP_GamePlayBase`)는 `ELayerName::Main`으로 등록하고, 팀원 행(`WBP_TeamHealthRow`)을 위 규칙대로 채운다.

`WidgetClasses`에 등록하지 않은 이름으로 `ShowWidget`을 부르면 로그에 다음이 찍히고 아무 창도 열리지 않는다.

```text
[UI] LayerName %d 위젯 클래스가 %s의 WidgetClasses에 없다.
```

## 테스트 및 확인 내용

- [x] 에디터 / Game 타겟 빌드
- [x] 로비 씬에서 창 열기 / 재활용 동작 (#47 로비 화면들이 이 스택 위에서 동작)
- [x] 게임플레이 씬에서 레이어 동작 확인
- [x] 리슨 서버 2인 PIE에서 자기 체력바가 서버 / 클라 양쪽 모두 초기값부터 표시되는지 확인
- [x] 팀원 체력바에서 자신이 제외되는지 확인

## 남은 작업

- 창을 열 때 마우스 커서 표시를 켜는 처리 추가
- PlayerController에 ESC 키 바인딩을 이관
- 옵션 창 내용 자체 (해상도, 사운드, 키 바인딩) — #50

## 확장 방향

```text
UI
 ├─ 레이어 전환 애니메이션
 ├─ 모달 창의 하위 입력 차단
 ├─ 진행 UI(#49), 설정/옵션 UI(#50)를 같은 레이어 스택 위에 구성
 └─ Vitals에 상태이상 / 탄약 등 표시용 사본 추가 (구조체 하나에 묶어 OnRep 1회 유지)
```
