#pragma once

#include "CoreMinimal.h"
#include "GameCore/UI/LayerWidgetBase.h"

#include "DefaultWidgetLayer.generated.h"

// ë² ì´???„ì— ?“ì´???¼ë°˜ ?ˆì´?? ESCë¡?pop ?œë‹¤.
UCLASS(Abstract)
class UDefaultWidgetLayer : public ULayerWidgetBase
{
	GENERATED_BODY()

public:
	// ???ˆì´???ˆì˜ ë²„íŠ¼?ì„œ ?¤ë¥¸ ?ˆì´?´ë? ?„ìš¸ ???´ë‹¤
	UFUNCTION(BlueprintCallable, Category = "Layer")
	UDefaultWidgetLayer* ShowWidget(ELayerName Name);

	// ?¤íƒ ë§????˜ë‚˜ë¥?pop ?˜ê³  Collapsed ?œí‚¨??
	UFUNCTION(BlueprintCallable, Category = "Layer")
	void CloseWidget();
};
