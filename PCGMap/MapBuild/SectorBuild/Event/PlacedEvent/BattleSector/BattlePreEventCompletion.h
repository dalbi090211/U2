#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "UObject/Interface.h"
#include "BattlePreEventCompletion.generated.h"

/**
 * Optional completion contract used only when a battle sector must wait for a
 * pre-event before it starts its encounter. Regular sector events remain
 * synchronous and do not need to implement this interface.
 */
UINTERFACE(MinimalAPI)
class UBattlePreEventCompletion : public UInterface
{
	GENERATED_BODY()
};

class IBattlePreEventCompletion
{
	GENERATED_BODY()

public:
	virtual void ExecuteBattlePreEvent(
		const UObject* WorldContext,
		FSimpleDelegate OnCompleted) = 0;
};
