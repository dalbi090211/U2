#include "LevelTriggerEvent.h"

void ALevelTriggerEvent::BeginPlay()
{
	Super::BeginPlay();

	//권한 가드를 두지 않는다. 각 머신이 자기 BeginPlay에서 실행해야 늦게 로딩한 클라이언트도 연출을 놓치지 않는다
	Execute(this);
}
