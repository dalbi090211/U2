#pragma once

#include "CoreMinimal.h"
#include "MapStruct.h"

// 서버 전용. 시트를 받아 검증한다. 틀린 곳이 있으면 화면에 띄우고 마지막 정상본(Saved/WorldGenSheet)을 쓴다
FWorldGenSheet LoadWorldGenSheet();
