#include "SectorGenSystem.h"

#include "Kismet/GameplayStatics.h"
#include "TeamProject_Fin/PCGMap/MapBuild/MapStatic.h"

void SectorGenSystem::Initialize(TMap<ESectorType, TArray<TSubclassOf<ASector>>> nodes)
{
	GenNodes = MoveTemp(nodes);
}

void SectorGenSystem::MakeSector(UObject* InWorldContextObject, ESectorType type)
{
	
}

//임시 시스템 =================================================
void SectorGenSystem::DebugInitialize(TMap<ESectorType, TArray<FName>> levels)
{
	GenLevels = MoveTemp(levels);
}

void SectorGenSystem::MakeDebugSector(UObject* InWorldContextObject, ESectorType type)
{
	const TArray<FName>* Levels = GenLevels.Find(type);
	if (!Levels || Levels->Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MakeDebugSector] No levels registered for type %d"), (int32)type);
		return;
	}

	int32 RandomIndex = randStream.RandRange(0, Levels->Num() - 1);
	FName LevelName = (*Levels)[RandomIndex];

	StartLevelTransition(InWorldContextObject, LevelName);
}