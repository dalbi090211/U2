# RIFT RUNNERS — 김명석 개발 포트폴리오

Unreal Engine 5.8 기반 3인칭 협동 슈터 **RIFT RUNNERS**에서 김명석(`dalbi090211`)이 구현·수정한 C++ 코드와 단독 담당 이슈의 기술문서를 모았습니다.

- **주요 담당:** 클래스·스킬, 루트 및 맵 PCG, 전투 방 이벤트, UI와 진행 흐름
- **원본 프로젝트:** [2021Cyan/TeamProject3](https://github.com/2021Cyan/TeamProject3)
- **수록 기준:** 2026-10-21 로컬 소스, 원본 커밋 [`265059d8`](https://github.com/2021Cyan/TeamProject3/commit/265059d8a3305a45c8f6c890590f4074069a3a88)
- **자료 성격:** 팀 프로젝트에서 기여한 부분을 검토하기 위한 코드 발췌 저장소입니다. 에셋·플러그인·전체 의존 코드는 포함하지 않아 이 저장소만으로 실행하거나 빌드할 수는 없습니다.

## 담당 업무

| 영역 | 구현 내용 | 주요 코드 |
|---|---|---|
| 루트 PCG | 시드 기반 경로 생성, 교차 간선 제외, Pre/Post 규칙, 방 타입별 가중치 및 미등장 보정 | [RouteGenSystem.cpp](PCGMap/MapBuild/RouteBuild/RouteGenSystem.cpp), [RouteEvent.h](PCGMap/MapBuild/RouteBuild/RouteEvent.h) |
| 맵 PCG | 문 접점 기준 DFS 조립, 검사 볼륨 생성과 겹침 검사, 끝 방 분산 배치, 생성 결과 평가 | [SectorSubsystem.cpp](PCGMap/MapBuild/SectorBuild/SectorSubsystem.cpp), [Sector.cpp](PCGMap/MapBuild/SectorBuild/Sector/Struct/Sector.cpp) |
| 생성 설정 데이터 | 구글 시트 CSV 읽기, 항목·범위 검증, 마지막 정상본과 코드 기본값으로 대체 | [WorldGenSheet.cpp](PCGMap/MapBuild/WorldGenSheet.cpp) |
| 전투 방·이벤트 | 공통 Execute 인터페이스, PreEvents/PostEvents와 지연 실행, 오버랩·전투 완료 이벤트, 화면 문구와 상호작용 | [SectorEvent.h](PCGMap/MapBuild/SectorBuild/Event/SectorEvent.h), [EventManager.cpp](PCGMap/MapBuild/SectorBuild/Event/EventManager.cpp), [Interact](Interact/) |
| 폭파병 스킬 | 투척·기폭 입력 분기, 노티파이 시점 스폰, 서버 폭발·피해·반동 처리 | [GA_BomberSkill_Mine.cpp](Ability/GA_BomberSkill_Mine.cpp), [BomberMine.cpp](Ability/BomberMine.cpp) |
| 스나이퍼 스킬 | 장막·은신 상태 분리, 발사 시 해제와 재진입, 강화 효과, 머티리얼·로컬 화면 연출 | [GA_SniperSkill_Camo.cpp](Ability/GA_SniperSkill_Camo.cpp), [GCN_StealthMaterial.cpp](Ability/GameplayCues/GCN_StealthMaterial.cpp) |
| UI·진행 흐름 | 위젯 레이어와 재사용, 경로 UI·선택 투표, 로비·캐릭터 선택, 세션 참가 및 트래블 연동 | [UI](UI/), [맵 UI](PCGMap/MapBuild/UI/), [Match](Match/) |

멀티플레이 기반과 캐릭터·무기·AI 연동은 공동 작업 파일에도 기여했습니다. 이 파일들은 본인 변경분의 맥락을 보존하기 위해 전체 파일로 수록했으며, 파일 전체를 단독 작성했다는 의미는 아닙니다. 파일별 원본 경로와 선정 근거는 [소스 출처 목록](Docs/SourceMap.md)에 기록했습니다.

## 단독 담당 이슈와 기술문서

2026-10-21 GitHub 이슈의 assignees가 `dalbi090211` 한 명인 항목만 아래에 정리했습니다. 이슈의 Closed 상태는 원본 관리 상태이며, 이번 복제 작업에서 기능을 새로 테스트했다는 의미는 아닙니다.

| 이슈 | 담당 업무 | 상태 | 기술문서 |
|---|---|---|---|
| [#20](https://github.com/2021Cyan/TeamProject3/issues/20) | [Class] 돌격병 클래스 구현 | Closed | 원본에 별도 기술문서 없음 |
| [#22](https://github.com/2021Cyan/TeamProject3/issues/22) | [Class] 폭파병 클래스 구현 | Closed | 원본에 별도 기술문서 없음 |
| [#24](https://github.com/2021Cyan/TeamProject3/issues/24) | [Skill] 폭파병 특이점 스킬 구현 | Closed | [기술문서](Docs/Technical/Issue-24-Bomber-Mine.md) |
| [#25](https://github.com/2021Cyan/TeamProject3/issues/25) | [Class] 스나이퍼 클래스 구현 | Closed | 원본에 별도 기술문서 없음 |
| [#26](https://github.com/2021Cyan/TeamProject3/issues/26) | [Skill] 스나이퍼 적 투시 스킬 구현 | Closed | [기술문서](Docs/Technical/Issue-26-Sniper-Camo.md) |
| [#41](https://github.com/2021Cyan/TeamProject3/issues/41) | [Map] 노드 기반 PCG 맵 생성 구현 | Closed | [기술문서](Docs/Technical/Issue-41-PCG-Map-Generation.md) |
| [#42](https://github.com/2021Cyan/TeamProject3/issues/42) | [Map] 전투 방 구현 | Closed | [기술문서](Docs/Technical/Issue-42-Battle-Room.md) |
| [#43](https://github.com/2021Cyan/TeamProject3/issues/43) | [Map] 휴식/상점 방 구현 | Closed | 원본에 별도 기술문서 없음 |
| [#45](https://github.com/2021Cyan/TeamProject3/issues/45) | [Map] 사건 방 구현 | Closed | 원본에 별도 기술문서 없음 |
| [#48](https://github.com/2021Cyan/TeamProject3/issues/48) | [UI] 전투 HUD 구현 | Closed | [기술문서](Docs/Technical/Issue-48-Combat-HUD.md) |
| [#49](https://github.com/2021Cyan/TeamProject3/issues/49) | [UI] 진행 UI 구현 | Closed | 원본에 별도 기술문서 없음 |

기술문서가 없는 이슈는 새 문서를 만들어 원본인 것처럼 수록하지 않았습니다. 세 클래스 관련 이슈의 전체 작업에는 Blueprint·에셋 설정도 포함되지만, 이 저장소에는 확인 가능한 C++ 기여분만 담았습니다.

### 공동 담당 범위

프로젝트 세팅 [#1](https://github.com/2021Cyan/TeamProject3/issues/1), 서버 통합 [#47](https://github.com/2021Cyan/TeamProject3/issues/47), UI 고도화 [#50](https://github.com/2021Cyan/TeamProject3/issues/50), 최종 빌드·테스트 [#51](https://github.com/2021Cyan/TeamProject3/issues/51)는 공동 할당입니다. 특히 #47 기술문서에 본인 이름이 적혀 있어도 실제 이슈는 공동 할당이므로, 단독 담당 기술문서 복제에서는 제외했습니다.

## 주요 문제와 대응

| 문제 | 구현한 대응 | 관련 자료 |
|---|---|---|
| 피스 피벗과 문 접점이 달라 맵 조립 시 위치가 어긋남 | 문 메시의 전면 중심·바닥을 접점으로 사용하고, 위치·회전 기준 좌표계로 스케일을 크기에 유지 | [SectorSubsystem.cpp](PCGMap/MapBuild/SectorBuild/SectorSubsystem.cpp), [Sector.cpp](PCGMap/MapBuild/SectorBuild/Sector/Struct/Sector.cpp) |
| 구글 시트 요청 실패나 잘못된 설정이 맵 생성으로 전파됨 | CSV 이름·중복·형식·범위를 검증하고 정상 데이터만 저장, 실패 시 정상본 → 기본값 순서로 대체 | [WorldGenSheet.cpp](PCGMap/MapBuild/WorldGenSheet.cpp) |
| 서버 이벤트가 호스트 화면에만 보이거나 UI 생성 전에 문구가 전달됨 | GameState의 멀티캐스트로 전달하고 활성 문구를 보관·재전달, 레벨 전환 시 남은 상태 정리 | [#42 기술문서](Docs/Technical/Issue-42-Battle-Room.md) |
| 클라이언트 HUD가 초기 체력 변경 신호를 놓침 | 초기화 후 현재값 전달, 위젯은 이벤트 구독 후 현재값을 한 번 읽어 갱신 | [#48 기술문서](Docs/Technical/Issue-48-Combat-HUD.md) |
| 지뢰 투척 시점이 모션과 어긋나거나 쿨다운이 기폭 입력까지 차단함 | 투척 노티파이와 예비 타이머를 단일 ReleaseMine 경로로 연결하고, 설치 상태에서는 기폭 입력의 쿨다운 검사 예외 처리 | [#24 기술문서](Docs/Technical/Issue-24-Bomber-Mine.md) |
| 은신 해제 후 원본 머티리얼 복구와 로컬 화면 효과의 적용 범위 관리 | 원본 머티리얼을 보관·복구하고, 공유 머티리얼 연출과 로컬 플레이어 전용 포스트프로세스를 구분 | [#26 기술문서](Docs/Technical/Issue-26-Sniper-Camo.md) |

## 한계와 개선 방향

- **루트별 특수방 분포:** 미등장 보정은 노드 순회 순서에 기반하므로 개별 플레이 경로의 특수방 개수·간격을 보장하지 않습니다. 경로별 평가와 플레이 데이터에 기반한 가중치 조정을 개선 방향으로 두었습니다.
- **맵 전체 형태 제어:** 문 연결을 따라 확장하는 방식으로, 분기 합류·순환 구조를 의도적으로 만드는 데 한계가 있습니다. 가변 연결부와 분기 재연결 규칙을 검토할 수 있습니다.
- **생성 결과 평가:** 배치 영역·피스 크기 변화·경로 깊이를 점수화하지만, 이 점수와 실제 플레이 경험의 연관성은 별도 검증이 필요합니다.
- **이벤트 등록:** 인터페이스를 구현하는 새 액터 계열이 늘면 AllowedClasses 목록을 수정해야 합니다. 인터페이스 기준의 선택기 필터링을 개선안으로 두었습니다.

위 항목은 현재 구현의 제한과 후속 아이디어이며, 구현 완료 기능으로 표시하지 않았습니다.

## 코드와 문서 읽는 방법

- U2의 기존 구조를 유지하여 원본 `Source/TeamProject_Fin/GameCore/` 아래 코드는 저장소 루트로 옮겼습니다. 예를 들어 `GameCore/Match/`는 `Match/`에 대응합니다. include 경로는 원본 그대로입니다.
- C++ 코드는 원본 파일을 그대로 복제했습니다. 원본에서 삭제된 이전 SectorGenSystem 계열 파일은 최신 구조와 섞이지 않도록 이번 갱신에서 정리했습니다. 이전 버전은 Git 이력에 남아 있습니다.
- [기술문서](Docs/Technical/)는 본문을 보존한 작성 당시 기록입니다. 제외된 공동 담당 문서를 참조하는 링크 한 곳만 원본 저장소 URL로 변경했습니다. 특히 #41의 초기 섹터 생성 설명·파일명과 #26 등의 남은 작업은 현재 코드와 다를 수 있습니다. 최신 구현은 위 코드 링크를 기준으로 확인해 주세요.
- 원본 저장소·이슈 링크는 GitHub 접근 권한이 필요할 수 있습니다.

## 이번 갱신의 확인 범위

- GitHub 단독 담당 여부와 원본의 본인 작성·수정 이력을 확인했습니다.
- 복제 파일의 SHA-256 일치, README 내부 파일 링크, 기술문서 선정 범위를 검사했습니다.
- 기존 코드에 들어 있는 자동화 테스트는 함께 수록했지만 실행하지 않았습니다. Unreal 빌드, 에셋 설정 및 멀티플레이 플레이 테스트는 이번 갱신 범위에 포함하지 않았습니다.
