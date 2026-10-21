# RIFT RUNNERS

Unreal Engine 5.8 기반 3인칭 협동 슈터 **RIFT RUNNERS**에서 김명석(`dalbi090211`)이 구현·수정한 C++ 코드와 단독 담당 이슈의 기술문서를 모았습니다.

- **주요 담당:** 클래스·스킬, 루트 및 맵 PCG, 전투 방 이벤트, UI와 진행 흐름
- **자료 성격:** 팀 프로젝트에서 기여한 부분을 보여주기 위한 코드 발췌 저장소입니다. 에셋·플러그인·전체 의존 코드는 포함하지 않아 이 저장소만으로 실행하거나 빌드할 수는 없습니다.

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

캐릭터·무기·AI 연동은 본인 변경분의 맥락을 보존하기 위해 전체 파일로 수록했으며, 파일 전체를 단독 작성했다는 의미는 아닙니다. 파일별 원본 경로와 선정 근거는 [소스 출처 목록](Docs/SourceMap.md)에 기록했습니다.

## 단독 담당 이슈와 기술문서

제가 담당한 기능의 기술문서들을 정리했습니다

| 이슈 | 담당 업무 | 상태 | 기술문서 |
|---|---|---|---|
| #24 | 폭파병 특이점 스킬 구현 | Closed | [기술문서](Docs/Technical/Issue-24-Bomber-Mine.md) |
| #26 | 스나이퍼 적 투시 스킬 구현 | Closed | [기술문서](Docs/Technical/Issue-26-Sniper-Camo.md) |
| #41 |  노드 기반 PCG 맵 생성 구현 | Closed | [기술문서](Docs/Technical/Issue-41-PCG-Map-Generation.md) |
| #42, #44, #45 | 이벤트 시스템 구현 | Closed | [기술문서](Docs/Technical/Issue-42-Battle-Room.md) |
| #48, #49 | UI Layer 시스템 구현 | Closed | [기술문서](Docs/Technical/Issue-48-Combat-HUD.md) |
