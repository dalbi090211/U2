#pragma once

#include "RouteEvent.h"
#include "TeamProject_Fin/PCGMap/MapBuild/MapStruct.h"

class RouteGenSystem
{
public:
	void Initialize(const TArray<FSectorWeight>& Weights);
	void generateRoute(FVirRoute*& OutVirtualRoute);
	
private :
	TArray<FSectorWeight> DefaultWeights;
	
	void makeOneRoute(int startX, FVirRoute*& OutVirtualRoute);
	void makeRoom(FVirRoute*& OutVirtualRoute);
	void postRulesExecute(FVirRoute*& OutVirtualRoute);
	void preRulesExecute(FVirRoute*& OutVirtualRoute);
	
	TArray<TSharedPtr<IRouteEvent>> PreRouteRules;
	TArray<TSharedPtr<IRouteEvent>> PostRouteRules;
	
	int Direction[3] = { -1, 0, 1 };
};
