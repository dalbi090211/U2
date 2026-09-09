#pragma once
#include "TeamProject_Fin/PCGMap/MapBuild/MapStruct.h"

class SectorGenSystem
{
public:
	void Initialize(TMap<ESectorType, TArray<TSubclassOf<ASector>>> nodes);
	void MakeSector(UObject* InWorldContextObject, ESectorType type);
	
private :
	TMap<ESectorType, TArray<TSubclassOf<ASector>>> GenNodes;
	
//임시 시스템 =================================================
public:
	void DebugInitialize(TMap<ESectorType, TArray<FName>> levels);
	void MakeDebugSector(UObject* InWorldContextObject, ESectorType type);
	
private:
	TMap<ESectorType, TArray<FName>> GenLevels;
};
