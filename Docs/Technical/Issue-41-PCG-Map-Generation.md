# #41 노드 기반 PCG 맵 생성 구현

## Issue

- Issue 번호: #41
- 기능명: 노드 기반 PCG 맵 생성 구현
- 담당자: dalbi090211
- 담당 영역: PCG Map / Route Generation / Map UI / Level Travel
- 관련 Sprint: 4주차

## 목적

로그라이크 진행 구조를 위해 런(run)마다 서로 다른 맵 경로를 생성한다.

맵 전체를 한 번에 만들지 않고 두 단계로 나눈다.

1. **가상 루트 생성** : 어떤 방이 몇 층에 있고 어디로 연결되는지를 그래프 데이터로만 만든다. 레벨이나 액터를 건드리지 않는다.
2. **섹터 실체화** : 플레이어가 선택한 노드의 타입에 맞는 실제 공간을 만든다.

두 단계를 분리하면 맵 UI, 이동 검증, 방 종류별 밸런스는 가상 루트만 보면 되고, 실제 지형 생성 방식(레벨 로드 / 섹터 조립)은 나중에 교체할 수 있다.

이번 단계의 구현 범위는 다음과 같다.

- 시드 기반 결정론적 루트 그래프 생성
- 규칙(Command) 기반 특수 방 배치 (시작 전투방, 중간 상점, 보스 전 휴식, 보스층)
- 가중치 기반 방 종류 할당
- 루트 그래프를 그리는 맵 선택 UI
- 노드 선택 검증 및 레벨 이동
- 포탈 상호작용을 통한 맵 UI 열기
- 리슨 서버 환경에서 호스트/클라이언트 동기화

실제 섹터 조립(`ASector` 배치, 소켓 연결)은 아직 디버그 경로(사전 제작 테스트 레벨 트래블)로 대체되어 있으며 후속 작업으로 분리한다.

## 주요 구조

```text
UWorldGenSubSystem (GameInstanceSubsystem)
 ├─ RouteGenSystem        가상 루트 그래프 생성
 │   └─ IRouteEvent[]     특수 방 배치 규칙 (Command)
 ├─ SectorGenSystem       섹터 실체화 (현재는 디버그 레벨 트래블)
 └─ FVirRoute*            생성된 루트 데이터 (서버/클라 각자 보유)
        ↑
ABasePlayerController      Client/Server RPC 경계
 └─ URouteWidget           루트 시각화, 노드/간선 위젯
     └─ URouteNode         노드 클릭 → Server_SelectNode
```

전체 흐름은 다음과 같다.

```text
포탈 상호작용 (AMapTransObject)
 └─ 서버: Client_OpenRouteMap(Seed, CurPos)
     └─ 클라: EnsureRoute(Seed) → 동일 루트 재생성
         └─ URouteWidget 표시, 이동 가능 노드 활성화
             └─ 노드 클릭 → Server_SelectNode(Pos)
                 └─ 서버: CanMoveTo 검증
                     └─ ChangeLevel → ServerTravel
```

## 구현 파일

### 서브시스템 / 설정

- `Source/TeamProject_Fin/PCGMap/MapBuild/WorldGenSubSystem.h/.cpp`
- `Source/TeamProject_Fin/PCGMap/MapBuild/WorldGenSettings.h`
- `Source/TeamProject_Fin/PCGMap/MapBuild/MapStruct.h`
- `Source/TeamProject_Fin/PCGMap/MapBuild/MapStatic.h`

### 루트 생성

- `Source/TeamProject_Fin/PCGMap/MapBuild/RouteBuild/RouteGenSystem.h/.cpp`
- `Source/TeamProject_Fin/PCGMap/MapBuild/RouteBuild/RouteEvent.h`

### 섹터 생성

- `Source/TeamProject_Fin/PCGMap/MapBuild/SectorBuild/SectorGenSystem.h/.cpp`
- `Source/TeamProject_Fin/PCGMap/MapBuild/SectorBuild/Sector.h/.cpp`
- `Source/TeamProject_Fin/PCGMap/MapBuild/SectorBuild/SectorSocketComponent.h`
- `Source/TeamProject_Fin/PCGMap/MapBuild/SectorBuild/SectorVolumeComponent.h`

### UI

- `Source/TeamProject_Fin/PCGMap/MapBuild/UI/RouteWidget.h/.cpp`
- `Source/TeamProject_Fin/PCGMap/MapBuild/UI/RouteNode.h/.cpp`

### 상호작용 / 네트워크 연결

- `Source/TeamProject_Fin/PCGMap/MapTransObject.h/.cpp`
- `Source/TeamProject_Fin/GameCore/BasePlayerController.h/.cpp`
- `Source/TeamProject_Fin/GameCore/Interact/InteractComponent.h/.cpp`
- `Source/TeamProject_Fin/GameCore/Interface/Interactable.h`

### Blueprint / Asset

- `Content/PCGMap/Map/WBP_RouteWidget`
- `Content/PCGMap/Map/WBP_NodeWidget`
- `Content/PCGMap/Map/WBP_RouteDot`
- `Content/PCGMap/Map/BP_PortalTest`
- `Content/PCGMap/Map/BP_RouteWidget`
- `Content/PCGMap/Map/PCGTestMap`
- `Content/PCGMap/Map/TestMaps/Alloc`, `NotAlloc`, `Monster`, `Random`, `Store`, `EliteMonster`, `Boss`
- `Content/PCGMap/Map/UI/Icon_Elite_256`, `Icon_RandomRoom_256`, `Icon_Shop_256_1`

## 데이터 구조

루트는 층(Level) 배열이고 각 층은 섹터(Sector) 배열이다.

```text
FVirRoute
 ├─ DefaultEnvType   환경 타입 (현재 Lab)
 ├─ RouteCount       생성할 경로 수
 ├─ MaxX, MaxY       루트 폭 / 높이
 └─ Levels[MaxY]
     └─ Sectors[MaxX]
         ├─ Type              ESectorType
         ├─ ConnectedSectors  다음 층에서 연결되는 X 좌표 목록
         └─ bVisited
```

`ConnectedSectors`는 **상대 방향이 아니라 다음 층의 절대 X 좌표**를 담는다. 상대 오프셋을 쓰면 보스층처럼 층 구조가 바뀌는 규칙에서 좌표 계산이 어긋나기 때문이다.

`ESectorType`은 방의 종류이자 생성 파이프라인의 상태값이다.

```text
None    노드 없음
Allot   경로에 포함되었지만 아직 종류 미할당
Monster / Event / Elite / Store / Boss
```

`Allot`은 "이 칸은 경로에 포함됨"을 뜻하는 중간 상태다. 방 종류 할당은 `Allot`인 칸만 대상으로 하므로, 규칙에서 미리 확정한 방(보스, 상점 등)은 확률 할당에 다시 휩쓸리지 않는다.

## 루트 생성 알고리즘

`RouteGenSystem::generateRoute()`가 아래 순서로 실행된다.

```text
1. Levels / Sectors 초기화 (MaxY x MaxX)
2. 시작 지점 선정
3. 시작 지점마다 경로 1개씩 생성
4. 남은 경로는 기존 시작 지점 중 하나를 재사용해 생성
5. Pre 규칙 실행 (특수 방 확정)
6. 가중치 기반 방 할당 (Allot 대상)
7. Post 규칙 실행
```

### 시작 지점 선정

```cpp
int StartCount = randStream.RandRange(2, FMath::Min(RouteCount, MaxX - 1));
```

시작 지점은 최소 2개이며 중복 좌표는 다시 뽑는다. 경로 수보다 시작 지점이 적으므로 여러 경로가 같은 지점에서 갈라져 나오는 형태가 된다.

### 경로 1개 생성

`makeOneRoute()`는 시작 X에서 위로 한 층씩 올라가며 다음을 수행한다.

- 현재 칸을 `Allot`으로 표시
- 마지막 층이면 간선을 만들지 않고 종료
- 이동 방향을 `{-1, 0, +1}` 중 무작위 순서로 시도

방향 선택에는 두 가지 제약이 있다.

```text
1. 범위 밖(nextX < 0 || nextX >= MaxX)은 건너뛴다
2. 목표 칸이 이미 현재 칸으로 오는 간선을 가지고 있으면 건너뛴다
```

두 번째 조건이 대각선 간선끼리 X자로 교차하는 경우를 막는다. 교차한 간선은 UI에서 어느 노드로 이어지는지 구분되지 않아 선택 UI가 무의미해진다.

간선은 `AddUnique`로 추가해 같은 경로가 여러 번 지나가도 중복되지 않는다.

### 방 종류 할당

`makeRoom()`은 `Allot` 칸마다 누적 가중치 룰렛을 돌린다.

```cpp
float selectWeight = randStream.RandRange(0.f, WeightSum);
for (const FSectorWeight& Weight : DefaultWeights)
{
    selectWeight -= Weight.Weight;
    if (selectWeight <= KINDA_SMALL_NUMBER) { /* 이 타입으로 확정 */ }
}
```

부동소수 오차로 어떤 타입도 선택되지 않으면 경고 로그를 남기고 `Monster`로 대체해 빈 노드가 생기지 않도록 한다.

## 규칙(Command) 기반 특수 방 배치

정적으로 실행되어야 할 방 생성 규칙은 `IRouteEvent` 인터페이스를 구현한 Command로 분리한다. 규칙을 추가할 때 생성 알고리즘 본문을 수정하지 않고 배열에 등록만 하면 되고, 규칙에 실행 타이밍에 따라 각각 PreRouteRules, PostRouteRules에 등록한다.

```cpp
class IRouteEvent {
public:
    virtual void Execute(FVirRoute*& OutVirtualRoute) = 0;
};
```

현재 등록된 규칙은 다음과 같다.

| 규칙 | 내용 |
|---|---|
| `MakeRouteStartCommand` | 0층의 `Allot`을 전부 `Monster`로 확정 (첫 방은 반드시 전투) |
| `MakeRouteStoreCommand` | 중간 층(`MaxY / 2`)을 `Store`로 확정 |
| `BeforeBossRestCommand` | 최상단 일반 층(`MaxY - 1`)을 `Store`로 확정 (보스 직전 휴식) |
| `MakeBossRoomCommand` | 최상단 위에 보스 전용 층을 추가하고 기존 최상단 노드를 전부 보스방으로 연결 |

규칙은 방 할당(`makeRoom`) **이전에** 실행된다. 규칙에서 타입을 `Allot`이 아닌 값으로 확정해두면 이후 확률 할당이 해당 칸을 건드리지 않기 때문이다.

## 결정론과 시드 관리

멀티플레이에서 서버와 클라이언트가 각자 루트를 생성하므로 결과가 반드시 동일해야 한다. 루트 데이터를 통째로 복제하는 대신 **시드만 전달하고 같은 알고리즘을 양쪽에서 실행**한다.

```text
서버: GetRunSeed()      최초 호출 시 시드를 뽑고 루트 생성
클라: EnsureRoute(Seed) 서버에게 받은 시드로 동일 루트 생성
```

`EnsureRoute()`는 같은 시드로 이미 만들었으면 아무 것도 하지 않는다. 레벨 트래블로 서브시스템이 유지된 채 재진입해도 루트가 다시 생성되지 않는다.

`World Gen` 설정의 `Seed`가 0이면 런마다 무작위, 0이 아니면 해당 값으로 고정된다. 재현이 필요한 테스트는 이 값을 고정해 사용한다.

UI 노드의 지터(Jitter)는 전역 `randStream`이 아니라 `URouteWidget`이 별도로 보유한 `LayoutRand`에서 뽑는다.

```cpp
void URouteWidget::Init(FVirRoute* OutVirtualRoute, int32 LayoutSeed);
```

전역 스트림을 그대로 쓰면 머신마다 소비 횟수가 달라져 노드 위치가 어긋나므로, 레이아웃 난수는 런 시드로 초기화한 독립 스트림에서 뽑는다.

(추후 전역 스트림을 폐기하는 방향으로 개선 예정)

## 이동 검증

이동 가능 여부 판정은 서버의 `UWorldGenSubSystem::CanMoveTo()` 한 곳에 있다.

```text
1. 좌표가 유효하고 해당 칸이 None이 아닐 것
2. curPos.Y < 0 (런 시작)이면 0층 아무 노드나 가능
3. 그 외에는 정확히 한 층 위(pos.Y == curPos.Y + 1)일 것
4. 현재 칸의 ConnectedSectors에 목표 X가 포함될 것
```

`ChangeLevel()`은 여기에 더해 두 가지를 막는다.

- 클라이언트에서의 트래블 시작 (`NM_Client`면 즉시 반환)
- 같은 월드에서의 중복 트래블 (`TravelStartedIn` 가드, 먼저 누른 플레이어가 이긴다)

`TravelStartedIn`은 월드 포인터를 들고 있으므로 트래블 후 월드가 바뀌면 자동으로 무효가 된다. 별도 리셋 코드가 필요 없다.

## 네트워크 흐름

UI는 플레이어별 로컬 객체이므로 Multicast가 아니라 Client RPC로 연다.

```text
플레이어 상호작용 키
 └─ UInteractComponent::Invoke()
     └─ 권한 없으면 Server_Invoke()
         └─ 서버: 가장 가까운 IInteractable 의 Execute(PC)
             └─ AMapTransObject::Execute()
                 └─ BPC->Client_OpenRouteMap(RunSeed, CurPos)

클라이언트
 └─ EnsureRoute(Seed)
 └─ 위젯 생성 및 Init
 └─ ActiveWidget(CurPos) / VisibleUI(CurPos)
 └─ InputMode UIOnly, 마우스 커서 표시

노드 클릭
 └─ URouteNode::Select()
     └─ PC->Server_SelectNode(nodePos)
         └─ 서버: UWorldGenSubSystem::ChangeLevel(pos)
             └─ ServerTravel(LevelName, bAbsolute = false)
```

트래블은 `bAbsolute = false`(relative)로 수행한다. absolute 트래블은 클라이언트에게 "새 서버로 붙어라"는 의미라 호스트만 이동하고 클라이언트는 세션이 끊긴다.

입력 모드는 맵을 열 때 `UIOnly`로 바뀌므로, 되돌리는 짝을 `ABasePlayerController::BeginPlay()`에 둔다. 트래블 방식에 의존하지 않고 매 레벨마다 명시적으로 `GameOnly`로 복원한다.

## 맵 UI

`URouteWidget`은 `CanvasPanel` 위에 노드와 간선 점을 직접 배치한다.

### 노드

- `None` 타입은 위젯을 만들지 않고 위치만 계산해둔다 (간선 계산에 필요)
- 노드 위치는 격자 좌표 + 지터
- 앵커/피벗을 중앙으로 두어 좌표계를 화면 중심 기준으로 통일
- 노드 ZOrder 1, 간선 점 ZOrder 0

### 간선

간선은 선(Line)이 아니라 점(Dot) 위젯의 나열로 그린다.

```text
PointCount = max(2, round(Length / dotSpacing))
t = p / PointCount
fadeWeight = sin(t * PI)
FinalPoint = Lerp(Start, End) + Perpendicular * Rand(-edgeNoiseRange, edgeNoiseRange) * fadeWeight
```

`sin(t * PI)` 가중치 때문에 노드에 가까울수록 흔들림이 줄어들어 간선이 노드에 정확히 붙는다. 점 위젯은 진행 방향으로 회전시켜 길쭉한 이미지도 사용할 수 있다.

### 조작 및 표시

- `SetPosition(Y)` : 현재 층이 화면에 오도록 컨테이너를 스크롤
- 마우스 드래그로 세로 스크롤 (`NativeOnMouseButtonDown/Move/Up`, 캡처 후 클램프)
- `ActiveWidget(pos)` : `pos.Y < 0`이면 0층 전체, 그 외에는 `ConnectedSectors` 노드만 활성화
- 위젯은 `NativeConstruct`에서 `Hidden`으로 시작하고 포탈 상호작용 시 표시

노드 외형은 `URouteNode::OnTextureSet(Type)` (BlueprintImplementableEvent)에서 타입별로 지정하므로 방 종류가 늘어도 C++ 수정 없이 대응할 수 있다.

## 섹터 실체화

`SectorGenSystem`은 두 경로를 가진다.

```text
isDebug = true   MakeDebugSector()  타입별 사전 제작 레벨 중 하나로 ServerTravel
isDebug = false  MakeSector()       ASector 배치 방식 (미구현, 추후 PCG 방식으로 구현 예정)
```

정식 경로에서 사용할 요소는 정의되어 있다.

- `ASector` : 조립 단위 액터. 볼륨/소켓은 BP에서 Add Component로 부착하고 코드에서 수집한다.
- `USectorSocketComponent` : 입구/출구 연결 지점 (`ArrowComponent` 기반, Front/Back)
- `USectorVolumeComponent` : 섹터 점유 영역 (`BoxComponent` 기반, 충돌 없음)
- `DoAABB()` : 섹터 배치 시 영역 겹침 검사 (10.f 허용 오차)

현재는 방 종류별 동작 검증이 목적이므로 디버그 경로만 사용한다.

## 설정

`UWorldGenSettings`(`UDeveloperSettings`, `Config = Game`)로 프로젝트 설정 `World Gen`에서 편집한다.

| 항목 | 설명 |
|---|---|
| `GenNodes` | 타입별 `ASector` 클래스 목록 (정식 경로용) |
| `GenLevelNames` | 타입별 디버그 레벨 이름 목록 |
| `RouteWidgetClass` | 루트 맵 위젯 클래스 |
| `isDebug` | 디버그 레벨 트래블 사용 여부 |
| `Seed` | 0이면 런마다 랜덤, 그 외에는 고정 |
| `SectorInterval` | 섹터 배치 간격 (정식 경로용) |
| `DefaultWeights` | 방 종류별 생성 가중치 |


루트 크기는 설정이 아니라 `UWorldGenSubSystem`의 상수로 고정되어 있다.

```cpp
static constexpr int32 RouteCount = 6;   // 생성할 경로 수
static constexpr int32 RouteMaxX  = 7;   // 루트 폭
static constexpr int32 RouteMaxY  = 15;  // 루트 높이(보스층 제외)
```

## 사용 방법

### 새 방 종류 추가

1. `ESectorType`에 값을 추가한다.
2. `World Gen` 설정의 `GenLevelNames`(디버그) 에 해당 타입 항목을 등록한다.
3. `DefaultWeights`에 가중치를 추가한다. 확률 할당 대상이 아니면 등록하지 않는다.
4. 노드 위젯 BP를 만들고 `OnTextureSet`에서 타입별 아이콘을 지정 후 메인 위젯에 등록한다.

### 특수 규칙 추가

`RouteEvent.h`에 `IRouteEvent` 구현을 추가하고 `RouteGenSystem::Initialize()`의 규칙 배열에 등록한다.

```cpp
class MyRuleCommand : public IRouteEvent {
public:
    virtual void Execute(FVirRoute*& OutVirtualRoute) override
    {
        // Allot 인 칸만 바꾸면 이후 확률 할당에서 제외된다
    }
};
```

### 맵에 포탈 배치

1. `AMapTransObject` 기반 BP(`BP_PortalTest`)를 레벨에 배치한다.
2. Mesh에 표시용 에셋을 지정한다. Trigger 반경 기본값은 250이다.
3. 플레이어는 `UInteractComponent`를 보유해야 한다 (`APlayerCharacter`에 기본 부착).
4. 트리거 범위 안에서 상호작용 키를 누르면 루트 맵이 열린다.

### 루트 확인 로그

`DebugRoute()`가 생성 직후 루트를 텍스트로 출력한다. 위층이 먼저 나온다.

```text
[DebugRoute] Y=15 | . . . B . . .
[DebugRoute] Y=14 | . S S . . . .
[DebugRoute] Y=13 | . M . E . . .
```

```text
. None   A Allot   M Monster   E Event
L Elite  S Store   B Boss
```

## 테스트 및 확인 내용

### 테스트 에셋

- `Content/PCGMap/Map/PCGTestMap`
- `Content/PCGMap/Map/TestMaps/*` (타입별 확인용 레벨)
- `BP_PortalTest`
- `WBP_RouteWidget`, `WBP_NodeWidget`, `WBP_RouteDot`

### 완료 조건 확인

- [x] 게임 시작 시 노드 맵이 생성됨
- [x] 시작 방과 보스 방이 고정됨
- [x] 중간 방이 규칙에 따라 랜덤 배치됨

## 남은 작업

- `FSectorWeight::Level`을 이용한 층별 확률 보정 (현재 `DefaultWeights`만 사용)
- 플레이 경험을 위한 ui 작업(페이드 인 아웃, 다른 플레이어의 ui에도 효과 적용 등)
- 플레이 경험을 위한 인게임 작업(포탈에 들어가고 나오는 모션 등)
- IRouteEvent 규칙을 추가하여 환경 이벤트 추가
- 랜덤맵 제작

## 확장 방향

확장은 두 단계로 나뉜다.

```text
PCG 기반 섹터 생성
 └─ SectorGenSystem
     ├─ ASector 조립 방식 구현
     ├─ 소켓 기반 연결 및 겹침 회피
     └─ 환경별 섹터 풀 교체

디자인 랜덤 맵 생성
 └─ IRandomSector(임시)
     ├─ 인질 구출 및 탈출
     ├─ 시간 내 처치
     ├─ 웨이브 방어
     └─ 특정 컨셉방(점프맵, 호러) 탈출
```