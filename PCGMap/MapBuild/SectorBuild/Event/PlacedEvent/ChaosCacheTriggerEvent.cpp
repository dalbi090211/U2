#include "ChaosCacheTriggerEvent.h"

AChaosCacheTriggerEvent::AChaosCacheTriggerEvent()
{
	CacheMode = ECacheMode::Play;
	StartMode = EStartMode::Triggered;	//BeginPlay에 터지지 않고 Execute를 기다린다
}
