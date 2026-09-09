#pragma once

#include "Sector.generated.h"

UCLASS()
class ASector : public AActor
{
	GENERATED_BODY()
public:
	ASector();
	
	//둘 다 bp에서 Add Component를 통해 부착하고, 코드에서 템플릿을 가져와 이용함.(ex) auto Consume = [&Out](const USceneComponent* Comp))
	// List<SectorVolumeComponent> Volumes	//Sector간 연결 시 overlap 되는 경우를 피하기 위해 생성한 볼륨
	// List<SectorSecoketComponent> Sockets	//Sector간 연결 시 입구/출구 간 연결을 위해 생성한 소켓
	
};
