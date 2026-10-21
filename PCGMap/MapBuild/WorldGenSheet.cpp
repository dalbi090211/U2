#include "WorldGenSheet.h"

#include "WorldGenSettings.h"
#include "Engine/Engine.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"

namespace
{
	using FRow = TArray<const TCHAR*>;

	FString CachePath(const TCHAR* Tab)
	{
		return FPaths::ProjectSavedDir() / TEXT("WorldGenSheet") / FString(Tab) + TEXT(".csv");
	}

	// 런 시작에서 한 번뿐이라 게임 스레드를 막고 기다린다. 4초를 넘기면 엔진이 요청을 끊는다
	bool Fetch(const FString& Url, FString& OutCsv, TArray<FString>& Errors)
	{
		if (Url.IsEmpty())
		{
			Errors.Add(TEXT("시트 주소 없음. 프로젝트 설정 → World Gen → Sheet"));
			return false;
		}

		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));
		Request->ProcessRequest();
		FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Default);

		const FHttpResponsePtr Response = Request->GetResponse();
		if (!Response || Response->GetResponseCode() != 200)
		{
			Errors.Add(FString::Printf(TEXT("시트를 못 읽음 (코드 %d): %s"),
									   Response ? Response->GetResponseCode() : 0, *Url));
			return false;
		}

		OutCsv = Response->GetContentAsString();
		return true;
	}

	FString CellAt(const FRow& Row, int32 Col)
	{
		return Row.IsValidIndex(Col) ? FString(Row[Col]).TrimStartAndEnd() : FString();
	}

	// 빈 칸과 $ 로 시작하는 칸이 첫머리인 줄/열은 메모로 보고 건너뛴다
	bool IsMemo(const FString& Cell)
	{
		return Cell.IsEmpty() || Cell.StartsWith(TEXT("$"));
	}

	int32 FindColumn(const FRow& Header, const TCHAR* Name)
	{
		for (int32 Col = 0; Col < Header.Num(); Col++)
		{
			if (CellAt(Header, Col).Equals(Name, ESearchCase::IgnoreCase))
			{
				return Col;
			}
		}
		return INDEX_NONE;
	}

	// Atoi 는 "1.5" 를 1 로, "abc" 를 0 으로 조용히 바꾼다
	bool ParseInt(const FString& Cell, int32& Out)
	{
		if (!Cell.IsNumeric() || Cell.Contains(TEXT(".")))
		{
			return false;
		}
		Out = FCString::Atoi(*Cell);
		return true;
	}

	// 세로 표. Name 열의 이름이 FWorldGenSheet 의 int 필드 이름과 같아야 한다
	void ParseValues(const FCsvParser::FRows& Rows, FWorldGenSheet& Out, TArray<FString>& Errors)
	{
		const int32 NameCol = FindColumn(Rows[0], TEXT("Name"));
		const int32 ValueCol = FindColumn(Rows[0], TEXT("Value"));
		if (NameCol == INDEX_NONE || ValueCol == INDEX_NONE)
		{
			Errors.Add(TEXT("[수치] 첫 줄에 Name, Value 열이 있어야 함"));
			return;
		}

		TSet<FName> Seen;
		for (int32 Row = 1; Row < Rows.Num(); Row++)
		{
			const FString Name = CellAt(Rows[Row], NameCol);
			if (IsMemo(Name))
			{
				continue;
			}

			const FIntProperty* Prop =
				CastField<FIntProperty>(FWorldGenSheet::StaticStruct()->FindPropertyByName(FName(*Name)));
			if (!Prop)
			{
				Errors.Add(FString::Printf(TEXT("[수치] %d행: 모르는 이름 '%s'"), Row + 1, *Name));
				continue;
			}
			if (Seen.Contains(Prop->GetFName()))
			{
				Errors.Add(FString::Printf(TEXT("[수치] %d행: %s 가 두 번 나옴"), Row + 1, *Name));
				continue;
			}
			Seen.Add(Prop->GetFName());

			const FString Cell = CellAt(Rows[Row], ValueCol);
			int32 Value = 0;
			if (!ParseInt(Cell, Value))
			{
				Errors.Add(FString::Printf(TEXT("[수치] %d행 %s: 정수가 아님 '%s'"), Row + 1, *Name, *Cell));
				continue;
			}
			Prop->SetPropertyValue_InContainer(&Out, Value);
		}

		for (TFieldIterator<FIntProperty> It(FWorldGenSheet::StaticStruct()); It; ++It)
		{
			if (!Seen.Contains(It->GetFName()))
			{
				Errors.Add(FString::Printf(TEXT("[수치] 빠진 이름: %s"), *It->GetName()));
			}
		}
	}

	// 여기서 안 막으면 루트·조립 코드 깊은 곳에서 무한 루프나 범위 밖 접근으로 터진다
	void CheckRanges(const FWorldGenSheet& Sheet, TArray<FString>& Errors)
	{
		const auto Require = [&Errors](bool bOk, const TCHAR* Rule, int32 Value)
		{
			if (!bOk)
			{
				Errors.Add(FString::Printf(TEXT("[수치] %s (지금 %d)"), Rule, Value));
			}
		};

		Require(Sheet.RouteCount >= 1, TEXT("RouteCount 는 1 이상"), Sheet.RouteCount);
		// 시작 지점을 서로 다른 칸으로 둘 뽑는다
		Require(Sheet.RouteMaxX >= 2, TEXT("RouteMaxX 는 2 이상"), Sheet.RouteMaxX);
		Require(Sheet.RouteMaxY >= 1, TEXT("RouteMaxY 는 1 이상"), Sheet.RouteMaxY);
		Require(Sheet.EndSectorCount >= 1, TEXT("EndSectorCount 는 1 이상"), Sheet.EndSectorCount);
		Require(Sheet.SectorPieceCount > Sheet.EndSectorCount,
				TEXT("SectorPieceCount 는 EndSectorCount 보다 커야 함"), Sheet.SectorPieceCount);
		Require(Sheet.MaxRouteLength >= 1, TEXT("MaxRouteLength 는 1 이상"), Sheet.MaxRouteLength);
	}

	// 첫 줄이 방 타입 이름, 그 아래 한 줄이 가중치. 빈 칸은 0. 층별 차이는 룰이 정한다
	void ParseWeights(const FCsvParser::FRows& Rows, FWorldGenSheet& Out, TArray<FString>& Errors)
	{
		const UEnum* TypeEnum = StaticEnum<ESectorType>();
		const FRow& Header = Rows[0];

		TArray<TPair<int32, ESectorType>> TypeCols;
		for (int32 Col = 0; Col < Header.Num(); Col++)
		{
			const FString Name = CellAt(Header, Col);
			if (IsMemo(Name))
			{
				continue;
			}

			// None/Allot 은 방이 아니라 할당 전 표식이다. 뽑히면 그 칸은 빈 방이 된다
			const int64 Type = TypeEnum->GetValueByNameString(Name);
			if (Type == INDEX_NONE || Type == (int64)ESectorType::None || Type == (int64)ESectorType::Allot)
			{
				Errors.Add(FString::Printf(TEXT("[확률] 방 타입이 아닌 열: '%s'"), *Name));
				continue;
			}
			if (TypeCols.ContainsByPredicate([Type](const TPair<int32, ESectorType>& P) { return (int64)P.Value == Type; }))
			{
				Errors.Add(FString::Printf(TEXT("[확률] %s 열이 두 번 나옴"), *Name));
				continue;
			}
			TypeCols.Emplace(Col, (ESectorType)Type);
		}

		// 첫 칸이 $ 이거나 통째로 빈 줄은 메모. 가중치 칸은 비어도 0 이라 첫 칸만으로는 못 가린다
		TArray<int32> DataRows;
		for (int32 Row = 1; Row < Rows.Num(); Row++)
		{
			const bool bAllEmpty = !Rows[Row].ContainsByPredicate([](const TCHAR* Cell) { return *Cell != TEXT('\0'); });
			if (!bAllEmpty && !CellAt(Rows[Row], 0).StartsWith(TEXT("$")))
			{
				DataRows.Add(Row);
			}
		}
		if (DataRows.Num() != 1)
		{
			Errors.Add(FString::Printf(TEXT("[확률] 가중치 줄은 하나여야 함 (지금 %d줄)"), DataRows.Num()));
			return;
		}

		const int32 Row = DataRows[0];
		for (const TPair<int32, ESectorType>& Col : TypeCols)
		{
			const FString Cell = CellAt(Rows[Row], Col.Key);
			const float Weight = Cell.IsEmpty() ? 0.f : FCString::Atof(*Cell);
			if (!Cell.IsEmpty() && (!Cell.IsNumeric() || Weight < 0.f))
			{
				Errors.Add(FString::Printf(TEXT("[확률] %d행 %s: 0 이상 숫자여야 함 '%s'"),
										   Row + 1, *CellAt(Header, Col.Key), *Cell));
				continue;
			}

			FSectorWeight& Entry = Out.Weights.AddDefaulted_GetRef();
			Entry.Type = Col.Value;
			Entry.Weight = Weight;
		}
	}

	void ParseSheet(const FString& ValuesCsv, const FString& WeightsCsv, FWorldGenSheet& Out, TArray<FString>& Errors)
	{
		const FCsvParser Values(ValuesCsv);
		const FCsvParser Weights(WeightsCsv);
		if (Values.GetRows().IsEmpty() || Weights.GetRows().IsEmpty())
		{
			Errors.Add(TEXT("빈 탭이 있음"));
			return;
		}

		ParseValues(Values.GetRows(), Out, Errors);
		// 이름이 빠지면 0 으로 남아 범위 오류가 덩달아 쏟아진다
		if (Errors.IsEmpty())
		{
			CheckRanges(Out, Errors);
		}
		ParseWeights(Weights.GetRows(), Out, Errors);
	}

	// 시트도 정상본도 못 쓸 때. 시트 양식 그대로라 같은 검증을 탄다
	const TCHAR* DefaultValuesCsv =
		TEXT("Name,Value\n")
		TEXT("RouteCount,6\n")
		TEXT("RouteMaxX,7\n")
		TEXT("RouteMaxY,5\n")
		TEXT("SectorPieceCount,22\n")
		TEXT("EndSectorCount,2\n")
		TEXT("MaxRouteLength,5\n");
	const TCHAR* DefaultWeightsCsv =
		TEXT("Monster,Event,Elite,Store\n")
		TEXT("45,68,0,0\n");

	void Notify(const FString& Message)
	{
		UE_LOG(LogTemp, Error, TEXT("[WorldGenSheet] %s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red, TEXT("[시트] ") + Message);
		}
	}

	// 정상본은 저장할 때 검증을 통과한 것이다. 그래도 틀리면 그 뒤에 코드 쪽 필드가 바뀐 것
	FWorldGenSheet LoadFallback()
	{
		FString ValuesCsv;
		FString WeightsCsv;
		FFileHelper::LoadFileToString(ValuesCsv, *CachePath(TEXT("Values")));
		FFileHelper::LoadFileToString(WeightsCsv, *CachePath(TEXT("Weights")));

		FWorldGenSheet Sheet;
		TArray<FString> Errors;
		ParseSheet(ValuesCsv, WeightsCsv, Sheet, Errors);
		if (Errors.IsEmpty())
		{
			Notify(TEXT("마지막 정상본으로 진행"));
			return Sheet;
		}

		Notify(TEXT("마지막 정상본도 없어 코드 기본값으로 진행"));
		Sheet = FWorldGenSheet();
		Errors.Reset();
		ParseSheet(DefaultValuesCsv, DefaultWeightsCsv, Sheet, Errors);
		checkf(Errors.IsEmpty(), TEXT("[WorldGenSheet] 코드 기본값이 틀림: %s"), Errors.IsEmpty() ? TEXT("") : *Errors[0]);
		return Sheet;
	}
}

FWorldGenSheet LoadWorldGenSheet()
{
	const UWorldGenSettings* Settings = GetDefault<UWorldGenSettings>();

	TArray<FString> Errors;
	FString ValuesCsv;
	FString WeightsCsv;
	FWorldGenSheet Sheet;
	if (Fetch(Settings->ValuesSheetUrl, ValuesCsv, Errors) && Fetch(Settings->WeightsSheetUrl, WeightsCsv, Errors))
	{
		ParseSheet(ValuesCsv, WeightsCsv, Sheet, Errors);
	}

	if (Errors.IsEmpty())
	{
		FFileHelper::SaveStringToFile(ValuesCsv, *CachePath(TEXT("Values")));
		FFileHelper::SaveStringToFile(WeightsCsv, *CachePath(TEXT("Weights")));
		return Sheet;
	}

	for (const FString& Error : Errors)
	{
		Notify(Error);
	}
	return LoadFallback();
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldGenSheetParseTest, "TeamProject.WorldGen.SheetParse",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenSheetParseTest::RunTest(const FString& Parameters)
{
	const FString Values =
		TEXT("Name,Value,$설명\n")
		TEXT("RouteCount,6,경로 수\n")
		TEXT("RouteMaxX,7,\n")
		TEXT("RouteMaxY,2,\n")
		TEXT("$아래는 섹터 조립,,\n")
		TEXT("SectorPieceCount,22,\n")
		TEXT("EndSectorCount,2,\n")
		TEXT("MaxRouteLength,5,\n");
	const FString Weights =
		TEXT("Monster,Event,Elite,$메모\n")
		TEXT("$층별 차이는 룰로,,,\n")
		TEXT("45,68,,빈 칸은 0\n");

	const auto Parse = [](const FString& V, const FString& W, FWorldGenSheet& Out)
	{
		TArray<FString> Errors;
		ParseSheet(V, W, Out, Errors);
		return Errors;
	};

	FWorldGenSheet Sheet;
	const TArray<FString> Errors = Parse(Values, Weights, Sheet);
	TestTrue(TEXT("정상 시트는 통과"), Errors.IsEmpty());
	TestEqual(TEXT("RouteMaxX"), Sheet.RouteMaxX, 7);
	TestEqual(TEXT("타입 3개"), Sheet.Weights.Num(), 3);
	FWorldGenSheet Defaults;
	TestTrue(TEXT("코드 기본값은 통과"), Parse(DefaultValuesCsv, DefaultWeightsCsv, Defaults).IsEmpty());

	FWorldGenSheet Ignored;
	TestFalse(TEXT("이름 오타"), Parse(Values.Replace(TEXT("RouteMaxX"), TEXT("RouteMaxx_")), Weights, Ignored).IsEmpty());
	TestFalse(TEXT("소수"), Parse(Values.Replace(TEXT("RouteMaxX,7"), TEXT("RouteMaxX,7.5")), Weights, Ignored).IsEmpty());
	TestFalse(TEXT("이름 중복"), Parse(Values + TEXT("RouteCount,3,\n"), Weights, Ignored).IsEmpty());
	TestFalse(TEXT("범위 밖"), Parse(Values.Replace(TEXT("RouteMaxX,7"), TEXT("RouteMaxX,1")), Weights, Ignored).IsEmpty());
	TestFalse(TEXT("가중치 줄 둘"), Parse(Values, TEXT("Monster\n45\n10\n"), Ignored).IsEmpty());
	TestFalse(TEXT("모르는 타입"), Parse(Values, TEXT("Monstr\n1\n"), Ignored).IsEmpty());
	TestFalse(TEXT("음수 가중치"), Parse(Values, TEXT("Monster\n-1\n"), Ignored).IsEmpty());
	return true;
}

#endif
