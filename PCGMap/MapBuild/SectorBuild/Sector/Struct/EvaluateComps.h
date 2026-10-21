#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "EvaluateComps.generated.h"

// 리플렉션용 스텁. 실제 내용은 IEvaluateComps 에 넣는다
UINTERFACE(MinimalAPI, Blueprintable)
class UEvaluateComps : public UInterface
{
	GENERATED_BODY()
};

class TEAMPROJECT_FIN_API IEvaluateComps
{
	GENERATED_BODY()

public:
};
