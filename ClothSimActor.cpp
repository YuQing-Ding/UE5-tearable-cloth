#include "ClothSimActor.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshResources.h"

namespace
{
    static FIntVector MakePinWeightPositionKey(const FVector& Position)
    {
        // Keep precision high enough to avoid collapsing neighboring cloth vertices.
        constexpr float QuantizeScale = 10000.0f; // 0.0001 units
        return FIntVector(
            FMath::RoundToInt(Position.X * QuantizeScale),
            FMath::RoundToInt(Position.Y * QuantizeScale),
            FMath::RoundToInt(Position.Z * QuantizeScale)
        );
    }
}

AClothSimActor::AClothSimActor()
{
    PrimaryActorTick.bCanEverTick = true;

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(SceneRoot);

    ClothMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ClothMesh"));
    ClothMesh->SetupAttachment(SceneRoot);
    ClothMesh->bUseAsyncCooking = true;
    ClothMesh->SetCastShadow(true);
    ClothMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ClothMesh->SetGenerateOverlapEvents(false);
}

void AClothSimActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildCloth();
}

void AClothSimActor::BeginPlay()
{
    Super::BeginPlay();

    if (Points.Num() <= 0 || Constraints.Num() <= 0 || !bMeshSectionCreated)
    {
        RebuildCloth();
    }
}

void AClothSimActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    HandleRuntimeParameterChanges();

    UWorld* World = GetWorld();
    if (World && !World->IsGameWorld())
    {
        UpdateRenderMesh();
        DebugDraw();
        return;
    }

    TimeAccumulator += DeltaTime;

    int32 Substeps = 0;
    while (TimeAccumulator >= FixedTimeStep && Substeps < MaxSubsteps)
    {
        SimulateStep(FixedTimeStep);
        TimeAccumulator -= FixedTimeStep;
        ++Substeps;
    }

    if (Substeps > 0)
    {
        bRenderVerticesDirty = true;
    }

    UpdateRenderMesh();
    DebugDraw();
}

#if WITH_EDITOR
bool AClothSimActor::ShouldTickIfViewportsOnly() const
{
    return true;
}
#endif

void AClothSimActor::UpdateRuntimeCache()
{
    CachedClothWidth = ClothWidth;
    CachedClothHeight = ClothHeight;
    CachedSpacing = Spacing;
    bCachedPinTopRow = bPinTopRow;
    bCachedHorizontalLayout = bHorizontalLayout;
    CachedClothOrigin = ClothOrigin;
    bCachedUseCustomStaticMesh = bUseCustomStaticMesh;
    CachedSourceStaticMesh = SourceStaticMesh;
    CachedSourceMeshScale = SourceMeshScale;
    bCachedCenterSourceMeshAtOrigin = bCenterSourceMeshAtOrigin;
    bCachedUseSourceMeshUV0 = bUseSourceMeshUV0;
    CachedClothShapeType = ClothShapeType;
    CachedShapeSizeUV = ShapeSizeUV;
    CachedShapeCenterUV = ShapeCenterUV;
    bCachedInvertShapeSelection = bInvertShapeSelection;
    bCachedUsePinRegion = bUsePinRegion;
    CachedPinRegionType = PinRegionType;
    CachedPinRegionCenter = PinRegionCenter;
    CachedPinRegionExtent = PinRegionExtent;
    CachedPinRegionRadius = PinRegionRadius;
    bCachedInvertPinRegion = bInvertPinRegion;
    bCachedUseExternalPinWeights = bUseExternalPinWeights;
    CachedExternalPinWeightFilePath = ExternalPinWeightFile.FilePath;
    CachedSoftPinStiffness = SoftPinStiffness;
    CachedClothMaterialPreset = ClothMaterialPreset;
    bCachedAutoApplyMaterialPreset = bAutoApplyMaterialPreset;
    CachedClothTotalMassKg = ClothTotalMassKg;
    bCachedUseAreaWeightedPointMass = bUseAreaWeightedPointMass;
    CachedMinPointMassKg = MinPointMassKg;
}

void AClothSimActor::HandleRuntimeParameterChanges()
{
    if (!bAutoRebuildOnRuntimeChange)
    {
        return;
    }

    const bool bNeedRebuild =
        CachedClothWidth != ClothWidth ||
        CachedClothHeight != ClothHeight ||
        !FMath::IsNearlyEqual(CachedSpacing, Spacing) ||
        bCachedPinTopRow != bPinTopRow ||
        bCachedHorizontalLayout != bHorizontalLayout ||
        !CachedClothOrigin.Equals(ClothOrigin, 0.01f) ||
        bCachedUseCustomStaticMesh != bUseCustomStaticMesh ||
        CachedSourceStaticMesh.Get() != SourceStaticMesh ||
        !CachedSourceMeshScale.Equals(SourceMeshScale, KINDA_SMALL_NUMBER) ||
        bCachedCenterSourceMeshAtOrigin != bCenterSourceMeshAtOrigin ||
        bCachedUseSourceMeshUV0 != bUseSourceMeshUV0 ||
        CachedClothShapeType != ClothShapeType ||
        !CachedShapeSizeUV.Equals(ShapeSizeUV, KINDA_SMALL_NUMBER) ||
        !CachedShapeCenterUV.Equals(ShapeCenterUV, KINDA_SMALL_NUMBER) ||
        bCachedInvertShapeSelection != bInvertShapeSelection ||
        bCachedUsePinRegion != bUsePinRegion ||
        CachedPinRegionType != PinRegionType ||
        !CachedPinRegionCenter.Equals(PinRegionCenter, KINDA_SMALL_NUMBER) ||
        !CachedPinRegionExtent.Equals(PinRegionExtent, KINDA_SMALL_NUMBER) ||
        !FMath::IsNearlyEqual(CachedPinRegionRadius, PinRegionRadius) ||
        bCachedInvertPinRegion != bInvertPinRegion ||
        bCachedUseExternalPinWeights != bUseExternalPinWeights ||
        CachedExternalPinWeightFilePath != ExternalPinWeightFile.FilePath ||
        !FMath::IsNearlyEqual(CachedSoftPinStiffness, SoftPinStiffness) ||
        CachedClothMaterialPreset != ClothMaterialPreset ||
        bCachedAutoApplyMaterialPreset != bAutoApplyMaterialPreset;

    if (bNeedRebuild)
    {
        RebuildCloth();
        return;
    }

    const bool bNeedMassRefresh =
        !FMath::IsNearlyEqual(CachedClothTotalMassKg, ClothTotalMassKg) ||
        bCachedUseAreaWeightedPointMass != bUseAreaWeightedPointMass ||
        !FMath::IsNearlyEqual(CachedMinPointMassKg, MinPointMassKg);

    if (bNeedMassRefresh)
    {
        ClothTotalMassKg = FMath::Max(0.001f, ClothTotalMassKg);
        MinPointMassKg = FMath::Max(0.000001f, MinPointMassKg);
        UpdatePointMasses();
        UpdateRuntimeCache();
    }
}

void AClothSimActor::RebuildCloth()
{
    Points.Reset();
    Constraints.Reset();
    ConstraintLookup.Reset();

    RenderVertices.Reset();
    RenderTriangles.Reset();
    RenderShadingTriangles.Reset();
    BaseTriangles.Reset();
    RenderNormals.Reset();
    RenderUV0.Reset();
    BaseUV0.Reset();
    RenderVertexColors.Reset();
    RenderTangents.Reset();

    DebugWorldHits.Reset();
    TimeAccumulator = 0.0f;

    PointInverseMasses.Reset();

    bTopologyDirty = true;
    bMeshSectionCreated = false;
    bRenderVerticesDirty = true;
    bUsingSourceMeshTopology = false;

    if (bAutoApplyMaterialPreset && ClothMaterialPreset != EClothMaterialPreset::Custom)
    {
        ApplyMaterialPreset(ClothMaterialPreset);
    }

    ClothWidth = FMath::Max(1, ClothWidth);
    ClothHeight = FMath::Max(1, ClothHeight);
    Spacing = FMath::Max(1.0f, Spacing);
    ShapeSizeUV.X = FMath::Clamp(ShapeSizeUV.X, 0.01f, 1.0f);
    ShapeSizeUV.Y = FMath::Clamp(ShapeSizeUV.Y, 0.01f, 1.0f);
    ShapeCenterUV.X = FMath::Clamp(ShapeCenterUV.X, 0.0f, 1.0f);
    ShapeCenterUV.Y = FMath::Clamp(ShapeCenterUV.Y, 0.0f, 1.0f);
    SourceMeshScale.X = FMath::Abs(SourceMeshScale.X);
    SourceMeshScale.Y = FMath::Abs(SourceMeshScale.Y);
    SourceMeshScale.Z = FMath::Abs(SourceMeshScale.Z);
    PinRegionExtent.X = FMath::Max(0.0f, PinRegionExtent.X);
    PinRegionExtent.Y = FMath::Max(0.0f, PinRegionExtent.Y);
    PinRegionExtent.Z = FMath::Max(0.0f, PinRegionExtent.Z);
    PinRegionRadius = FMath::Max(0.0f, PinRegionRadius);
    SoftPinStiffness = FMath::Clamp(SoftPinStiffness, 0.0f, 1.0f);
    SolverIterations = FMath::Max(1, SolverIterations);
    CollisionPassesWhenTearing = FMath::Clamp(CollisionPassesWhenTearing, 1, 64);
    FixedTimeStep = FMath::Max(0.001f, FixedTimeStep);
    MaxSubsteps = FMath::Max(1, MaxSubsteps);
    Stiffness = FMath::Clamp(Stiffness, 0.01f, 1.0f);
    ClothTotalMassKg = FMath::Max(0.001f, ClothTotalMassKg);
    MinPointMassKg = FMath::Max(0.000001f, MinPointMassKg);
    ClothWeightScale = FMath::Clamp(ClothWeightScale, 0.01f, 4.0f);
    Damping = FMath::Clamp(Damping, 0.0f, 1.0f);
    StretchCompliance = FMath::Max(0.0f, StretchCompliance);
    MaxStretchRatio = FMath::Max(1.0f, MaxStretchRatio);
    HangingAntiStretch = FMath::Clamp(HangingAntiStretch, 0.0f, 1.0f);
    NearInextensibleStretchLimit = FMath::Clamp(NearInextensibleStretchLimit, 1.0f, 1.05f);
    NearInextensibleStrength = FMath::Clamp(NearInextensibleStrength, 0.0f, 1.0f);
    NearInextensiblePasses = FMath::Clamp(NearInextensiblePasses, 1, 8);
    PostStepLengthRecoveryPasses = FMath::Clamp(PostStepLengthRecoveryPasses, 0, 32);
    PostStepLengthRecoveryStrength = FMath::Clamp(PostStepLengthRecoveryStrength, 0.0f, 1.0f);
    PointCollisionRadius = FMath::Max(0.1f, PointCollisionRadius);
    MinWorldCollisionMoveDistance = FMath::Max(0.0f, MinWorldCollisionMoveDistance);
    ClothToClothPointRadius = FMath::Max(0.1f, ClothToClothPointRadius);
    ClothToClothSurfaceOffset = FMath::Max(0.0f, ClothToClothSurfaceOffset);
    ClothToClothFriction = FMath::Clamp(ClothToClothFriction, 0.0f, 1.0f);
    TearDistance = FMath::Max(0.0f, TearDistance);
    DamageRadius = FMath::Max(1.0f, DamageRadius);
    UVTilingX = FMath::Max(0.001f, UVTilingX);
    UVTilingY = FMath::Max(0.001f, UVTilingY);
    TearEdgeSmoothStrength = FMath::Clamp(TearEdgeSmoothStrength, 0.0f, 1.0f);
    TearEdgeSmoothIterations = FMath::Clamp(TearEdgeSmoothIterations, 1, 6);
    TearStrandCountPerEdge = FMath::Clamp(TearStrandCountPerEdge, 1, 8);
    TearStrandThickness = FMath::Max(0.0f, TearStrandThickness);
    TearStrandSpread = FMath::Max(0.0f, TearStrandSpread);
    TearStrandIrregularity = FMath::Clamp(TearStrandIrregularity, 0.0f, 1.0f);
    TearStrandMaxStretchRatio = FMath::Max(1.0f, TearStrandMaxStretchRatio);
    MaxRenderedTearStrands = FMath::Clamp(MaxRenderedTearStrands, 1, 4096);

    ExternalPinWeights.Reset();
    ExternalPinWeightsByPosition.Reset();
    if (bUseExternalPinWeights)
    {
        LoadExternalPinWeights();
    }

    BuildPointsAndConstraints();
    UpdatePointMasses();

    if (bUseExternalPinWeights &&
        ExternalPinWeightsByPosition.Num() > 0)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("ClothSimActor '%s': Using position-based external pin weights (%d keys)."),
            *GetName(),
            ExternalPinWeightsByPosition.Num()
        );
    }
    else if (bUseExternalPinWeights &&
        ExternalPinWeightsByPosition.Num() <= 0 &&
        ExternalPinWeights.Num() > 0 &&
        ExternalPinWeights.Num() != Points.Num())
    {
        if (bUsingSourceMeshTopology)
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("ClothSimActor '%s': External pin weight count (%d) != render vertex count (%d). Mesh split vertices detected; fallback mapping is applied."),
                *GetName(),
                ExternalPinWeights.Num(),
                Points.Num()
            );
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("ClothSimActor '%s': External pin weight count (%d) does not match cloth point count (%d). Missing entries are treated as 0."),
                *GetName(),
                ExternalPinWeights.Num(),
                Points.Num()
            );
        }
    }

    ClothCollisionContactMask.SetNumZeroed(Points.Num());
    BuildConstraintLookup();
    InitializeRenderBuffers();
    RebuildTriangleIndexBuffer();
    UpdateRenderMesh();
    UpdateRuntimeCache();
}

void AClothSimActor::ApplySelectedMaterialPreset()
{
    ApplyMaterialPreset(ClothMaterialPreset);
    RebuildCloth();
}

void AClothSimActor::ReloadExternalPinWeights()
{
    RebuildCloth();
}

bool AClothSimActor::LoadExternalPinWeights()
{
    ExternalPinWeights.Reset();
    ExternalPinWeightsByPosition.Reset();

    if (!bUseExternalPinWeights)
    {
        return false;
    }

    const FString RawPath = ExternalPinWeightFile.FilePath.TrimStartAndEnd();
    if (RawPath.IsEmpty())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("ClothSimActor '%s': ExternalPinWeightFile is empty."),
            *GetName()
        );
        return false;
    }

    const FString ProjectDirAbs = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString LaunchDirAbs = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir());

    auto NormalizePath = [&ProjectDirAbs](FString InPath) -> FString
    {
        InPath = InPath.TrimStartAndEnd();
        InPath.ReplaceInline(TEXT("\""), TEXT(""));
        InPath.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (InPath.IsEmpty())
        {
            return InPath;
        }

        if (FPaths::IsRelative(InPath))
        {
            InPath = FPaths::ConvertRelativePathToFull(ProjectDirAbs, InPath);
        }

        FPaths::CollapseRelativeDirectories(InPath);
        FPaths::NormalizeFilename(InPath);
        return InPath;
    };

    TArray<FString> CandidatePaths;
    CandidatePaths.AddUnique(NormalizePath(RawPath));

    if (FPaths::IsRelative(RawPath))
    {
        CandidatePaths.AddUnique(NormalizePath(FPaths::ConvertRelativePathToFull(ProjectDirAbs, RawPath)));
        CandidatePaths.AddUnique(NormalizePath(FPaths::ConvertRelativePathToFull(LaunchDirAbs, RawPath)));
    }

#if PLATFORM_WINDOWS
    auto TryBuildWindowsUserPath = [&ProjectDirAbs](FString InPath, FString& OutPath) -> bool
    {
        if (ProjectDirAbs.Len() < 2 || ProjectDirAbs[1] != TCHAR(':'))
        {
            return false;
        }

        InPath = InPath.TrimStartAndEnd();
        InPath.ReplaceInline(TEXT("\""), TEXT(""));
        InPath.ReplaceInline(TEXT("\\"), TEXT("/"));

        while (InPath.StartsWith(TEXT("../")) || InPath.StartsWith(TEXT("..\\")))
        {
            InPath = InPath.Mid(3);
        }
        while (InPath.StartsWith(TEXT("./")))
        {
            InPath = InPath.Mid(2);
        }
        while (InPath.StartsWith(TEXT("/")))
        {
            InPath = InPath.Mid(1);
        }

        const int32 UsersPos = InPath.Find(TEXT("Users/"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
        if (UsersPos == INDEX_NONE)
        {
            return false;
        }

        const FString Tail = InPath.Mid(UsersPos);
        OutPath = ProjectDirAbs.Left(2) + TEXT("/") + Tail;
        return true;
    };

    if (!RawPath.Contains(TEXT(":")) && RawPath.StartsWith(TEXT("/")))
    {
        if (ProjectDirAbs.Len() >= 2 && ProjectDirAbs[1] == TCHAR(':'))
        {
            const FString DrivePrefixedPath = ProjectDirAbs.Left(2) + RawPath;
            CandidatePaths.AddUnique(NormalizePath(DrivePrefixedPath));
        }
    }

    FString WindowsUserCandidate;
    if (!RawPath.Contains(TEXT(":")) && TryBuildWindowsUserPath(RawPath, WindowsUserCandidate))
    {
        CandidatePaths.AddUnique(NormalizePath(WindowsUserCandidate));
    }
#endif

    FString FileContent;
    FString LoadedPath;
    bool bLoaded = false;
    for (const FString& CandidatePath : CandidatePaths)
    {
        if (CandidatePath.IsEmpty())
        {
            continue;
        }

        if (FFileHelper::LoadFileToString(FileContent, *CandidatePath))
        {
            bLoaded = true;
            LoadedPath = CandidatePath;
            break;
        }
    }

    if (!bLoaded)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("ClothSimActor '%s': Failed to read external pin weight file. Raw='%s' Tried='%s'"),
            *GetName(),
            *RawPath,
            *FString::Join(CandidatePaths, TEXT(" | "))
        );
        return false;
    }

    TArray<FString> Lines;
    FileContent.ParseIntoArrayLines(Lines, true);

    TArray<float> SequentialWeights;
    TMap<int32, float> IndexedWeights;
    int32 ParsedPositionRows = 0;
    int32 ParseErrors = 0;

    for (FString Line : Lines)
    {
        Line = Line.TrimStartAndEnd();
        if (Line.IsEmpty() ||
            Line.StartsWith(TEXT("#")) ||
            Line.StartsWith(TEXT("//")))
        {
            continue;
        }

        Line.ReplaceInline(TEXT(","), TEXT(" "));
        Line.ReplaceInline(TEXT(":"), TEXT(" "));
        Line.ReplaceInline(TEXT("="), TEXT(" "));
        Line.ReplaceInline(TEXT(";"), TEXT(" "));
        Line.ReplaceInline(TEXT("\t"), TEXT(" "));

        TArray<FString> Tokens;
        Line.ParseIntoArrayWS(Tokens);
        if (Tokens.Num() <= 0)
        {
            continue;
        }

        bool bConsumed = false;

        // Position mode:
        //   x y z weight
        //   index x y z weight
        if (Tokens.Num() == 4)
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
            float W = 0.0f;

            if (FDefaultValueHelper::ParseFloat(Tokens[0], X) &&
                FDefaultValueHelper::ParseFloat(Tokens[1], Y) &&
                FDefaultValueHelper::ParseFloat(Tokens[2], Z) &&
                FDefaultValueHelper::ParseFloat(Tokens[3], W))
            {
                ExternalPinWeightsByPosition.Add(
                    MakePinWeightPositionKey(FVector(X, Y, Z)),
                    FMath::Clamp(W, 0.0f, 1.0f)
                );
                ++ParsedPositionRows;
                bConsumed = true;
            }
        }
        else if (Tokens.Num() >= 5)
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
            float W = 0.0f;
            int32 ParsedIndex = INDEX_NONE;
            if (FDefaultValueHelper::ParseInt(Tokens[0], ParsedIndex) &&
                FDefaultValueHelper::ParseFloat(Tokens[1], X) &&
                FDefaultValueHelper::ParseFloat(Tokens[2], Y) &&
                FDefaultValueHelper::ParseFloat(Tokens[3], Z) &&
                FDefaultValueHelper::ParseFloat(Tokens[4], W))
            {
                const float ClampedWeight = FMath::Clamp(W, 0.0f, 1.0f);
                ExternalPinWeightsByPosition.Add(
                    MakePinWeightPositionKey(FVector(X, Y, Z)),
                    ClampedWeight
                );
                if (ParsedIndex >= 0)
                {
                    IndexedWeights.Add(ParsedIndex, ClampedWeight);
                }
                ++ParsedPositionRows;
                bConsumed = true;
            }
        }

        if (bConsumed)
        {
            continue;
        }

        if (Tokens.Num() == 2)
        {
            int32 ParsedIndex = INDEX_NONE;
            float ParsedWeight = 0.0f;
            const bool bValidIndex = FDefaultValueHelper::ParseInt(Tokens[0], ParsedIndex);
            const bool bValidWeight = FDefaultValueHelper::ParseFloat(Tokens[1], ParsedWeight);

            if (bValidIndex && bValidWeight && ParsedIndex >= 0)
            {
                IndexedWeights.Add(ParsedIndex, FMath::Clamp(ParsedWeight, 0.0f, 1.0f));
                continue;
            }
        }

        bool bParsedAnySequential = false;
        for (const FString& Token : Tokens)
        {
            float ParsedWeight = 0.0f;
            if (FDefaultValueHelper::ParseFloat(Token, ParsedWeight))
            {
                SequentialWeights.Add(FMath::Clamp(ParsedWeight, 0.0f, 1.0f));
                bParsedAnySequential = true;
            }
            else
            {
                ++ParseErrors;
            }
        }

        if (!bParsedAnySequential)
        {
            ++ParseErrors;
        }
    }

    if (IndexedWeights.Num() > 0)
    {
        int32 MaxIndex = INDEX_NONE;
        for (const TPair<int32, float>& Pair : IndexedWeights)
        {
            MaxIndex = FMath::Max(MaxIndex, Pair.Key);
        }

        if (MaxIndex >= 0)
        {
            ExternalPinWeights.SetNumZeroed(MaxIndex + 1);
            for (const TPair<int32, float>& Pair : IndexedWeights)
            {
                ExternalPinWeights[Pair.Key] = Pair.Value;
            }
        }
    }
    else
    {
        ExternalPinWeights = MoveTemp(SequentialWeights);
    }

    const int32 IndexedOrSequentialCount = ExternalPinWeights.Num();
    const int32 PositionKeyCount = ExternalPinWeightsByPosition.Num();
    if (IndexedOrSequentialCount <= 0 && PositionKeyCount <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("ClothSimActor '%s': No valid pin weights were parsed from file: %s"),
            *GetName(),
            *LoadedPath
        );
        return false;
    }

    if (ParseErrors > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("ClothSimActor '%s': Parsed external pin weights with %d invalid token(s): %s"),
            *GetName(),
            ParseErrors,
            *LoadedPath
        );
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("ClothSimActor '%s': Loaded external pin weights from %s (index/seq=%d, position=%d, parsedPositionRows=%d)"),
        *GetName(),
        *LoadedPath,
        IndexedOrSequentialCount,
        PositionKeyCount,
        ParsedPositionRows
    );

    if (PositionKeyCount > 0 && IndexedOrSequentialCount > 0)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("ClothSimActor '%s': Position-based pin weights take priority over index-based pin weights."),
            *GetName()
        );
    }

    return true;
}

float AClothSimActor::GetExternalPinWeight(int32 PointIndex) const
{
    if (!bUseExternalPinWeights ||
        !ExternalPinWeights.IsValidIndex(PointIndex))
    {
        return 0.0f;
    }

    return FMath::Clamp(ExternalPinWeights[PointIndex], 0.0f, 1.0f);
}

bool AClothSimActor::TryGetExternalPinWeightByPosition(const FVector& Position, float& OutWeight) const
{
    if (!bUseExternalPinWeights || ExternalPinWeightsByPosition.Num() <= 0)
    {
        return false;
    }

    const FIntVector BaseKey = MakePinWeightPositionKey(Position);
    if (const float* FoundWeight = ExternalPinWeightsByPosition.Find(BaseKey))
    {
        OutWeight = FMath::Clamp(*FoundWeight, 0.0f, 1.0f);
        return true;
    }

    // One-cell neighborhood fallback to absorb tiny import precision differences.
    for (int32 DZ = -1; DZ <= 1; ++DZ)
    {
        for (int32 DY = -1; DY <= 1; ++DY)
        {
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                if (DX == 0 && DY == 0 && DZ == 0)
                {
                    continue;
                }

                const FIntVector NeighborKey(BaseKey.X + DX, BaseKey.Y + DY, BaseKey.Z + DZ);
                if (const float* FoundWeight = ExternalPinWeightsByPosition.Find(NeighborKey))
                {
                    OutWeight = FMath::Clamp(*FoundWeight, 0.0f, 1.0f);
                    return true;
                }
            }
        }
    }

    return false;
}

float AClothSimActor::GetExternalPinWeightForPosition(const FVector& Position, int32 PointIndex) const
{
    float PositionWeight = 0.0f;
    if (TryGetExternalPinWeightByPosition(Position, PositionWeight))
    {
        return PositionWeight;
    }

    return GetExternalPinWeight(PointIndex);
}

float AClothSimActor::GetPointInverseMass(int32 PointIndex) const
{
    if (PointInverseMasses.IsValidIndex(PointIndex))
    {
        return FMath::Max(0.0f, PointInverseMasses[PointIndex]);
    }

    if (!Points.IsValidIndex(PointIndex) || Points[PointIndex].bPinned || !Points[PointIndex].bActive)
    {
        return 0.0f;
    }

    return 1.0f;
}

void AClothSimActor::ApplyMaterialPreset(EClothMaterialPreset Preset)
{
    switch (Preset)
    {
    case EClothMaterialPreset::Silk:
        ClothTotalMassKg = 0.25f;
        Damping = 0.985f;
        StretchCompliance = 0.000008f;
        MaxStretchRatio = 1.12f;
        HangingAntiStretch = 0.25f;
        ClothWeightScale = 0.75f;
        bUseNearInextensibleMode = false;
        NearInextensibleStretchLimit = 1.010f;
        NearInextensibleStrength = 0.0f;
        NearInextensiblePasses = 1;
        PostStepLengthRecoveryPasses = 0;
        PostStepLengthRecoveryStrength = 0.0f;
        bTreatWindAsForce = true;
        bUseAreaWeightedPointMass = true;
        break;

    case EClothMaterialPreset::Cotton:
        ClothTotalMassKg = 0.45f;
        Damping = 0.990f;
        StretchCompliance = 0.0000025f;
        MaxStretchRatio = 1.08f;
        HangingAntiStretch = 0.50f;
        ClothWeightScale = 1.0f;
        bUseNearInextensibleMode = true;
        NearInextensibleStretchLimit = 1.005f;
        NearInextensibleStrength = 0.75f;
        NearInextensiblePasses = 2;
        PostStepLengthRecoveryPasses = 6;
        PostStepLengthRecoveryStrength = 0.8f;
        bTreatWindAsForce = true;
        bUseAreaWeightedPointMass = true;
        break;

    case EClothMaterialPreset::Linen:
        ClothTotalMassKg = 0.55f;
        Damping = 0.992f;
        StretchCompliance = 0.0000018f;
        MaxStretchRatio = 1.06f;
        HangingAntiStretch = 0.65f;
        ClothWeightScale = 1.2f;
        bUseNearInextensibleMode = true;
        NearInextensibleStretchLimit = 1.004f;
        NearInextensibleStrength = 0.85f;
        NearInextensiblePasses = 2;
        PostStepLengthRecoveryPasses = 8;
        PostStepLengthRecoveryStrength = 0.9f;
        bTreatWindAsForce = true;
        bUseAreaWeightedPointMass = true;
        break;

    case EClothMaterialPreset::Wool:
        ClothTotalMassKg = 0.70f;
        Damping = 0.994f;
        StretchCompliance = 0.0000012f;
        MaxStretchRatio = 1.05f;
        HangingAntiStretch = 0.78f;
        ClothWeightScale = 1.35f;
        bUseNearInextensibleMode = true;
        NearInextensibleStretchLimit = 1.0035f;
        NearInextensibleStrength = 0.9f;
        NearInextensiblePasses = 3;
        PostStepLengthRecoveryPasses = 10;
        PostStepLengthRecoveryStrength = 0.95f;
        bTreatWindAsForce = true;
        bUseAreaWeightedPointMass = true;
        break;

    case EClothMaterialPreset::Denim:
        ClothTotalMassKg = 1.05f;
        Damping = 0.996f;
        StretchCompliance = 0.00000045f;
        MaxStretchRatio = 1.03f;
        HangingAntiStretch = 0.90f;
        ClothWeightScale = 2.0f;
        bUseNearInextensibleMode = true;
        NearInextensibleStretchLimit = 1.0022f;
        NearInextensibleStrength = 1.0f;
        NearInextensiblePasses = 4;
        PostStepLengthRecoveryPasses = 14;
        PostStepLengthRecoveryStrength = 1.0f;
        bTreatWindAsForce = true;
        bUseAreaWeightedPointMass = true;
        break;

    case EClothMaterialPreset::Canvas:
        ClothTotalMassKg = 1.40f;
        Damping = 0.997f;
        StretchCompliance = 0.00000025f;
        MaxStretchRatio = 1.02f;
        HangingAntiStretch = 0.97f;
        ClothWeightScale = 2.8f;
        bUseNearInextensibleMode = true;
        NearInextensibleStretchLimit = 1.0015f;
        NearInextensibleStrength = 1.0f;
        NearInextensiblePasses = 5;
        PostStepLengthRecoveryPasses = 18;
        PostStepLengthRecoveryStrength = 1.0f;
        bTreatWindAsForce = true;
        bUseAreaWeightedPointMass = true;
        break;

    case EClothMaterialPreset::Custom:
    default:
        break;
    }
}

void AClothSimActor::UpdatePointMasses()
{
    const int32 PointCount = Points.Num();
    PointInverseMasses.SetNumZeroed(PointCount);
    if (PointCount <= 0)
    {
        return;
    }

    const float TotalMassKg = FMath::Max(0.001f, ClothTotalMassKg);
    const float MinMassKg = FMath::Max(0.000001f, MinPointMassKg);

    int32 DynamicCount = 0;
    for (const FClothPoint& P : Points)
    {
        if (P.bActive && !P.bPinned)
        {
            ++DynamicCount;
        }
    }

    if (DynamicCount <= 0)
    {
        return;
    }

    bool bAssignedByArea = false;
    if (bUseAreaWeightedPointMass && bUsingSourceMeshTopology && BaseTriangles.Num() >= 3)
    {
        TArray<float> VertexAreaM2;
        VertexAreaM2.SetNumZeroed(PointCount);

        for (int32 T = 0; T + 2 < BaseTriangles.Num(); T += 3)
        {
            const int32 I0 = BaseTriangles[T];
            const int32 I1 = BaseTriangles[T + 1];
            const int32 I2 = BaseTriangles[T + 2];
            if (!Points.IsValidIndex(I0) || !Points.IsValidIndex(I1) || !Points.IsValidIndex(I2))
            {
                continue;
            }

            if (!Points[I0].bActive || !Points[I1].bActive || !Points[I2].bActive)
            {
                continue;
            }

            const FVector& A = Points[I0].Position;
            const FVector& B = Points[I1].Position;
            const FVector& C = Points[I2].Position;
            const float AreaCm2 = 0.5f * FVector::CrossProduct(B - A, C - A).Length();
            const float AreaM2 = AreaCm2 * 0.0001f; // cm^2 -> m^2
            if (AreaM2 <= KINDA_SMALL_NUMBER)
            {
                continue;
            }

            const float Share = AreaM2 / 3.0f;
            VertexAreaM2[I0] += Share;
            VertexAreaM2[I1] += Share;
            VertexAreaM2[I2] += Share;
        }

        float TotalAreaM2 = 0.0f;
        for (int32 i = 0; i < PointCount; ++i)
        {
            if (Points[i].bActive && !Points[i].bPinned)
            {
                TotalAreaM2 += VertexAreaM2[i];
            }
        }

        if (TotalAreaM2 > KINDA_SMALL_NUMBER)
        {
            for (int32 i = 0; i < PointCount; ++i)
            {
                const FClothPoint& P = Points[i];
                if (!P.bActive || P.bPinned)
                {
                    PointInverseMasses[i] = 0.0f;
                    continue;
                }

                const float Ratio = TotalAreaM2 > KINDA_SMALL_NUMBER ? (VertexAreaM2[i] / TotalAreaM2) : 0.0f;
                const float PointMassKg = FMath::Max(TotalMassKg * Ratio, MinMassKg);
                PointInverseMasses[i] = 1.0f / PointMassKg;
            }

            bAssignedByArea = true;
        }
    }

    if (!bAssignedByArea)
    {
        const float UniformMassKg = FMath::Max(TotalMassKg / static_cast<float>(DynamicCount), MinMassKg);
        const float UniformInvMass = 1.0f / UniformMassKg;
        for (int32 i = 0; i < PointCount; ++i)
        {
            const FClothPoint& P = Points[i];
            PointInverseMasses[i] = (P.bActive && !P.bPinned) ? UniformInvMass : 0.0f;
        }
    }
}

int32 AClothSimActor::GetPointIndex(int32 X, int32 Y) const
{
    return X + Y * (ClothWidth + 1);
}

uint64 AClothSimActor::MakeEdgeKey(int32 A, int32 B) const
{
    const uint32 MinIdx = static_cast<uint32>(FMath::Min(A, B));
    const uint32 MaxIdx = static_cast<uint32>(FMath::Max(A, B));
    return (static_cast<uint64>(MinIdx) << 32) | static_cast<uint64>(MaxIdx);
}

void AClothSimActor::BuildPointsAndConstraints()
{
    bUsingSourceMeshTopology = false;
    BaseTriangles.Reset();
    BaseUV0.Reset();

    if (bUseCustomStaticMesh && SourceStaticMesh)
    {
        if (BuildStaticMeshPointsAndConstraints())
        {
            bUsingSourceMeshTopology = true;
            return;
        }

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("ClothSimActor '%s': Failed to build from SourceStaticMesh '%s'. Falling back to grid. Make sure the mesh has render data and Allow CPU Access if needed."),
            *GetName(),
            *SourceStaticMesh->GetName()
        );
    }

    BuildGridPointsAndConstraints();
}

bool AClothSimActor::ShouldPointBeActive(int32 X, int32 Y, const FVector2D& UV) const
{
    (void)X;
    (void)Y;

    if (ClothShapeType == EClothShapeType::Rectangle &&
        ShapeSizeUV.Equals(FVector2D(1.0f, 1.0f), KINDA_SMALL_NUMBER) &&
        ShapeCenterUV.Equals(FVector2D(0.5f, 0.5f), KINDA_SMALL_NUMBER) &&
        !bInvertShapeSelection)
    {
        return true;
    }

    const float HalfSizeX = FMath::Max(0.005f, ShapeSizeUV.X * 0.5f);
    const float HalfSizeY = FMath::Max(0.005f, ShapeSizeUV.Y * 0.5f);
    const float LocalX = (UV.X - ShapeCenterUV.X) / HalfSizeX;
    const float LocalY = (UV.Y - ShapeCenterUV.Y) / HalfSizeY;

    bool bInside = true;
    switch (ClothShapeType)
    {
    case EClothShapeType::Rectangle:
        bInside = FMath::Abs(LocalX) <= 1.0f && FMath::Abs(LocalY) <= 1.0f;
        break;
    case EClothShapeType::Ellipse:
        bInside = (LocalX * LocalX + LocalY * LocalY) <= 1.0f;
        break;
    case EClothShapeType::Diamond:
        bInside = (FMath::Abs(LocalX) + FMath::Abs(LocalY)) <= 1.0f;
        break;
    case EClothShapeType::Triangle:
    {
        const bool bInVerticalRange = LocalY >= -1.0f && LocalY <= 1.0f;
        const float Width = (LocalY + 1.0f) * 0.5f;
        bInside = bInVerticalRange && FMath::Abs(LocalX) <= Width;
        break;
    }
    default:
        break;
    }

    return bInvertShapeSelection ? !bInside : bInside;
}

bool AClothSimActor::ShouldPinPoint(int32 X, int32 Y, const FVector& PositionLS, const FVector2D& UV) const
{
    if (bUsePinRegion)
    {
        const FVector Local = PositionLS - PinRegionCenter;
        bool bInside = false;

        if (PinRegionType == EPinRegionType::Box)
        {
            bInside =
                FMath::Abs(Local.X) <= PinRegionExtent.X &&
                FMath::Abs(Local.Y) <= PinRegionExtent.Y &&
                FMath::Abs(Local.Z) <= PinRegionExtent.Z;
        }
        else
        {
            bInside = Local.SizeSquared() <= FMath::Square(PinRegionRadius);
        }

        return bInvertPinRegion ? !bInside : bInside;
    }

    if (!bPinTopRow)
    {
        return false;
    }

    if (X >= 0 && Y >= 0)
    {
        return Y == 0;
    }

    return UV.Y >= 0.0f && UV.Y <= 0.02f;
}

void AClothSimActor::BuildGridPointsAndConstraints()
{
    const float Diagonal = Spacing * FMath::Sqrt(2.0f);
    const int32 PointCount = (ClothWidth + 1) * (ClothHeight + 1);
    const bool bUseWeightDrivenPin =
        bUseExternalPinWeights &&
        (ExternalPinWeights.Num() > 0 || ExternalPinWeightsByPosition.Num() > 0);

    Points.Reserve(PointCount);
    BaseUV0.Reserve(PointCount);

    const int32 EstimatedConstraints =
        (ClothHeight + 1) * ClothWidth +
        (ClothWidth + 1) * ClothHeight +
        (ClothWidth * ClothHeight * 2);
    Constraints.Reserve(EstimatedConstraints);

    for (int32 Y = 0; Y <= ClothHeight; ++Y)
    {
        for (int32 X = 0; X <= ClothWidth; ++X)
        {
            FClothPoint P;
            if (bHorizontalLayout)
            {
                P.Position = ClothOrigin + FVector(
                    static_cast<float>(X) * Spacing,
                    static_cast<float>(Y) * Spacing,
                    0.0f
                );
            }
            else
            {
                P.Position = ClothOrigin + FVector(
                    static_cast<float>(X) * Spacing,
                    0.0f,
                    -static_cast<float>(Y) * Spacing
                );
            }

            const FVector2D UV(
                static_cast<float>(X) / static_cast<float>(ClothWidth),
                static_cast<float>(Y) / static_cast<float>(ClothHeight)
            );

            P.PrevPosition = P.Position;
            P.Acceleration = FVector::ZeroVector;
            P.bActive = ShouldPointBeActive(X, Y, UV);
            const int32 CurrentIdx = GetPointIndex(X, Y);
            P.PinWeight = P.bActive ? GetExternalPinWeightForPosition(P.Position, CurrentIdx) : 0.0f;

            if (P.bActive && bUseWeightDrivenPin)
            {
                if (P.PinWeight >= (1.0f - KINDA_SMALL_NUMBER))
                {
                    P.bPinned = true;
                    P.PinPosition = P.Position;
                    P.PinWeight = 1.0f;
                }
                else if (P.PinWeight > 0.0f)
                {
                    P.PinPosition = P.Position;
                }
            }
            else if (P.bActive && ShouldPinPoint(X, Y, P.Position, UV))
            {
                P.bPinned = true;
                P.PinPosition = P.Position;
                P.PinWeight = 1.0f;
            }
            else if (P.bActive && P.PinWeight > 0.0f)
            {
                P.PinPosition = P.Position;
            }

            Points.Add(P);
            BaseUV0.Add(FVector2D(UV.X * UVTilingX, UV.Y * UVTilingY));

            if (X > 0)
            {
                const int32 LeftIdx = GetPointIndex(X - 1, Y);
                if (Points[CurrentIdx].bActive && Points[LeftIdx].bActive)
                {
                    FClothConstraint C;
                    C.P1 = CurrentIdx;
                    C.P2 = LeftIdx;
                    C.RestLength = Spacing;
                    C.Lambda = 0.0f;
                    Constraints.Add(C);
                }
            }

            if (Y > 0)
            {
                const int32 UpIdx = GetPointIndex(X, Y - 1);
                if (Points[CurrentIdx].bActive && Points[UpIdx].bActive)
                {
                    FClothConstraint C;
                    C.P1 = CurrentIdx;
                    C.P2 = UpIdx;
                    C.RestLength = Spacing;
                    C.Lambda = 0.0f;
                    Constraints.Add(C);
                }
            }

            if (X > 0 && Y > 0)
            {
                const int32 DiagA = GetPointIndex(X - 1, Y - 1);
                if (Points[CurrentIdx].bActive && Points[DiagA].bActive)
                {
                    FClothConstraint C1;
                    C1.P1 = CurrentIdx;
                    C1.P2 = DiagA;
                    C1.RestLength = Diagonal;
                    C1.Lambda = 0.0f;
                    Constraints.Add(C1);
                }

                const int32 DiagB0 = GetPointIndex(X - 1, Y);
                const int32 DiagB1 = GetPointIndex(X, Y - 1);
                if (Points[DiagB0].bActive && Points[DiagB1].bActive)
                {
                    FClothConstraint C2;
                    C2.P1 = DiagB0;
                    C2.P2 = DiagB1;
                    C2.RestLength = Diagonal;
                    C2.Lambda = 0.0f;
                    Constraints.Add(C2);
                }
            }
        }
    }
}

bool AClothSimActor::BuildStaticMeshPointsAndConstraints()
{
    if (!SourceStaticMesh)
    {
        return false;
    }

    const FStaticMeshRenderData* RenderData = SourceStaticMesh->GetRenderData();
    if (!RenderData || RenderData->LODResources.Num() <= 0)
    {
        return false;
    }

    const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
    const FPositionVertexBuffer& PositionBuffer = LOD.VertexBuffers.PositionVertexBuffer;
    const FStaticMeshVertexBuffer& StaticVertexBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;

    const int32 VertexCount = PositionBuffer.GetNumVertices();
    const int32 IndexCount = LOD.IndexBuffer.GetNumIndices();
    if (VertexCount <= 0 || IndexCount < 3)
    {
        return false;
    }

    Points.Reserve(VertexCount);
    BaseUV0.Reserve(VertexCount);
    BaseTriangles.Reserve(IndexCount);
    const bool bHasPositionWeights =
        bUseExternalPinWeights &&
        ExternalPinWeightsByPosition.Num() > 0;
    const bool bHasIndexedWeights =
        bUseExternalPinWeights &&
        ExternalPinWeights.Num() > 0;
    const bool bUseWeightDrivenPin = bHasPositionWeights || bHasIndexedWeights;
    const bool bNeedsWeldedWeightFallback =
        bHasIndexedWeights &&
        !bHasPositionWeights &&
        ExternalPinWeights.Num() != VertexCount;

    TMap<FIntVector, int32> WeldedPositionToWeightIndex;
    WeldedPositionToWeightIndex.Reserve(VertexCount);
    int32 WeldedUniqueCount = 0;

    const bool bHasUV0 = bUseSourceMeshUV0 && StaticVertexBuffer.GetNumTexCoords() > 0;
    const FVector MeshCenter = bCenterSourceMeshAtOrigin
        ? SourceStaticMesh->GetBoundingBox().GetCenter()
        : FVector::ZeroVector;

    for (int32 i = 0; i < VertexCount; ++i)
    {
        const FVector SourcePosition = FVector(PositionBuffer.VertexPosition(i));
        const FVector ScaledPos = SourcePosition - MeshCenter;
        const FVector PositionLS =
            FVector(
                ScaledPos.X * SourceMeshScale.X,
                ScaledPos.Y * SourceMeshScale.Y,
                ScaledPos.Z * SourceMeshScale.Z
            ) + ClothOrigin;

        FVector2D UV(-1.0f, -1.0f);
        if (bHasUV0)
        {
            UV = FVector2D(StaticVertexBuffer.GetVertexUV(i, 0));
        }

        FClothPoint P;
        P.Position = PositionLS;
        P.PrevPosition = PositionLS;
        P.Acceleration = FVector::ZeroVector;
        P.bActive = true;

        if (bUseWeightDrivenPin)
        {
            float PinWeight = 0.0f;
            bool bFoundWeight = false;

            if (bHasPositionWeights)
            {
                bFoundWeight =
                    TryGetExternalPinWeightByPosition(SourcePosition, PinWeight) ||
                    TryGetExternalPinWeightByPosition(ScaledPos, PinWeight) ||
                    TryGetExternalPinWeightByPosition(PositionLS, PinWeight);
            }

            if (!bFoundWeight && bNeedsWeldedWeightFallback)
            {
                const FIntVector PosKey = MakePinWeightPositionKey(SourcePosition);
                int32* FoundWeldedIndex = WeldedPositionToWeightIndex.Find(PosKey);
                if (!FoundWeldedIndex)
                {
                    const int32 NewWeldedIndex = WeldedUniqueCount++;
                    WeldedPositionToWeightIndex.Add(PosKey, NewWeldedIndex);
                    FoundWeldedIndex = WeldedPositionToWeightIndex.Find(PosKey);
                }

                const int32 WeightIndex = FoundWeldedIndex ? *FoundWeldedIndex : INDEX_NONE;
                PinWeight = ExternalPinWeights.IsValidIndex(WeightIndex)
                    ? FMath::Clamp(ExternalPinWeights[WeightIndex], 0.0f, 1.0f)
                    : 0.0f;
                bFoundWeight = true;
            }

            if (!bFoundWeight)
            {
                PinWeight = GetExternalPinWeight(i);
            }

            P.PinWeight = FMath::Clamp(PinWeight, 0.0f, 1.0f);
        }

        if (bUseWeightDrivenPin)
        {
            if (P.PinWeight >= (1.0f - KINDA_SMALL_NUMBER))
            {
                P.bPinned = true;
                P.PinPosition = PositionLS;
                P.PinWeight = 1.0f;
            }
            else if (P.PinWeight > 0.0f)
            {
                P.PinPosition = PositionLS;
            }
        }
        else if (ShouldPinPoint(-1, -1, PositionLS, UV))
        {
            P.bPinned = true;
            P.PinPosition = PositionLS;
            P.PinWeight = 1.0f;
        }
        else if (P.PinWeight > 0.0f)
        {
            P.PinPosition = PositionLS;
        }

        Points.Add(P);
        BaseUV0.Add(bHasUV0 ? FVector2D(UV.X * UVTilingX, UV.Y * UVTilingY) : FVector2D::ZeroVector);
    }

    if (bNeedsWeldedWeightFallback)
    {
        if (WeldedUniqueCount != ExternalPinWeights.Num())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("ClothSimActor '%s': External pin weights (%d) != render vertices (%d). Welded-position fallback produced %d unique keys; verify mesh/index parity or use position-based CSV."),
                *GetName(),
                ExternalPinWeights.Num(),
                VertexCount,
                WeldedUniqueCount
            );
        }
        else
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("ClothSimActor '%s': External pin weights (%d) mapped to %d render vertices via welded-position fallback."),
                *GetName(),
                ExternalPinWeights.Num(),
                VertexCount
            );
        }
    }

    TSet<uint64> AddedEdges;
    AddedEdges.Reserve(IndexCount);

    auto TryAddEdgeConstraint = [&](int32 A, int32 B)
    {
        if (!Points.IsValidIndex(A) || !Points.IsValidIndex(B) || A == B)
        {
            return;
        }

        const uint64 Key = MakeEdgeKey(A, B);
        if (AddedEdges.Contains(Key))
        {
            return;
        }

        AddedEdges.Add(Key);

        FClothConstraint C;
        C.P1 = A;
        C.P2 = B;
        C.RestLength = FVector::Dist(Points[A].Position, Points[B].Position);
        C.Lambda = 0.0f;
        Constraints.Add(C);
    };

    for (int32 Tri = 0; Tri + 2 < IndexCount; Tri += 3)
    {
        const int32 I0 = static_cast<int32>(LOD.IndexBuffer.GetIndex(Tri));
        const int32 I1 = static_cast<int32>(LOD.IndexBuffer.GetIndex(Tri + 1));
        const int32 I2 = static_cast<int32>(LOD.IndexBuffer.GetIndex(Tri + 2));

        if (!Points.IsValidIndex(I0) || !Points.IsValidIndex(I1) || !Points.IsValidIndex(I2))
        {
            continue;
        }

        BaseTriangles.Add(I0);
        BaseTriangles.Add(I1);
        BaseTriangles.Add(I2);

        TryAddEdgeConstraint(I0, I1);
        TryAddEdgeConstraint(I1, I2);
        TryAddEdgeConstraint(I2, I0);
    }

    return BaseTriangles.Num() >= 3;
}

void AClothSimActor::BuildConstraintLookup()
{
    ConstraintLookup.Reset();
    ConstraintLookup.Reserve(Constraints.Num());

    for (int32 i = 0; i < Constraints.Num(); ++i)
    {
        const FClothConstraint& C = Constraints[i];
        ConstraintLookup.Add(MakeEdgeKey(C.P1, C.P2), i);
    }
}

void AClothSimActor::InitializeRenderBuffers()
{
    const int32 VertexCount = Points.Num();

    RenderVertices.SetNum(VertexCount);
    RenderNormals.SetNum(VertexCount);
    RenderUV0.SetNum(VertexCount);
    RenderVertexColors.SetNum(VertexCount);
    RenderTangents.SetNum(VertexCount);

    for (int32 Idx = 0; Idx < VertexCount; ++Idx)
    {
        RenderVertices[Idx] = Points[Idx].Position;
        RenderNormals[Idx] = FVector::UpVector;
        RenderUV0[Idx] = BaseUV0.IsValidIndex(Idx) ? BaseUV0[Idx] : FVector2D::ZeroVector;
        RenderVertexColors[Idx] = FLinearColor::White;
        RenderTangents[Idx] = FProcMeshTangent(1.0f, 0.0f, 0.0f);
    }
}

bool AClothSimActor::BreakConstraint(FClothConstraint& Constraint)
{
    if (Constraint.bBroken)
    {
        return false;
    }

    Constraint.bBroken = true;
    Constraint.Lambda = 0.0f;
    bTopologyDirty = true;
    return true;
}

void AClothSimActor::SimulateStep(float Dt)
{
    DebugWorldHits.Reset();
    if (ClothCollisionContactMask.Num() != Points.Num())
    {
        ClothCollisionContactMask.SetNumZeroed(Points.Num());
    }
    else if (ClothCollisionContactMask.Num() > 0)
    {
        FMemory::Memzero(ClothCollisionContactMask.GetData(), ClothCollisionContactMask.Num());
    }

    Integrate(Dt);
    ResetConstraintLambdas();

    const bool bEnableAnyCollision =
        bEnableWorldCollision ||
        bUseLocalBoxCollider ||
        bEnableClothToClothCollision;
    int32 CollisionPasses = 0;
    if (bEnableAnyCollision)
    {
        CollisionPasses = bEnableTearing ? CollisionPassesWhenTearing : 1;
        CollisionPasses = FMath::Clamp(CollisionPasses, 1, SolverIterations);
    }

    int32 CollisionPassesDone = 0;

    for (int32 Iter = 0; Iter < SolverIterations; ++Iter)
    {
        SolveConstraints(Dt);

        if (CollisionPassesDone < CollisionPasses)
        {
            // evenly distribute collision passes, always including the last iteration
            const int32 NextCollisionIter =
                ((CollisionPassesDone + 1) * SolverIterations) / CollisionPasses - 1;

            if (Iter >= NextCollisionIter)
            {
                SolveCollisions();
                ++CollisionPassesDone;
            }
        }

        EnforceMaxStretch();
        EnforceNearInextensibleStretch();
    }

    while (CollisionPassesDone < CollisionPasses)
    {
        SolveCollisions();
        EnforceMaxStretch();
        EnforceNearInextensibleStretch();
        ++CollisionPassesDone;
    }

    // final safety pass
    EnforceMaxStretch();
    EnforceNearInextensibleStretch();

    // Extra substep-end recovery that is decoupled from SolverIterations.
    if (bUseNearInextensibleMode && PostStepLengthRecoveryPasses > 0 && PostStepLengthRecoveryStrength > KINDA_SMALL_NUMBER)
    {
        EnforceLengthRecovery(
            PostStepLengthRecoveryPasses,
            PostStepLengthRecoveryStrength,
            NearInextensibleStretchLimit
        );
    }

    RePinPoints();
}

void AClothSimActor::ResetConstraintLambdas()
{
    for (FClothConstraint& C : Constraints)
    {
        if (!C.bBroken)
        {
            C.Lambda = 0.0f;
        }
    }
}

void AClothSimActor::Integrate(float Dt)
{
    const FVector GravityForce(0.0f, 0.0f, -Gravity);

    for (int32 PointIdx = 0; PointIdx < Points.Num(); ++PointIdx)
    {
        FClothPoint& P = Points[PointIdx];
        if (!P.bActive)
        {
            continue;
        }

        if (P.bPinned)
        {
            P.Position = P.PinPosition;
            P.PrevPosition = P.PinPosition;
            P.Acceleration = FVector::ZeroVector;
            continue;
        }

        const float InvMass = GetPointInverseMass(PointIdx);
        if (InvMass <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        P.Acceleration += GravityForce;
        if (bTreatWindAsForce)
        {
            P.Acceleration += WindForce * InvMass;
        }
        else
        {
            P.Acceleration += WindForce;
        }

        const FVector Velocity = (P.Position - P.PrevPosition) * Damping;
        const FVector NewPosition = P.Position + Velocity + P.Acceleration * (Dt * Dt);

        P.PrevPosition = P.Position;
        P.Position = NewPosition;
        P.Acceleration = FVector::ZeroVector;
    }
}

void AClothSimActor::SolveConstraints(float Dt)
{
    for (FClothConstraint& C : Constraints)
    {
        SolveConstraintXPBD(C, Dt);
    }
}

void AClothSimActor::SolveCollisions()
{
    if (Points.Num() <= 0)
    {
        return;
    }

    if (bEnableWorldCollision)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            const FTransform ActorXform = GetActorTransform();

            FCollisionQueryParams Params(SCENE_QUERY_STAT(ClothPointSweep), false, this);
            Params.bReturnPhysicalMaterial = false;

            bool bRunPointSweeps = true;
            if (bUseWorldCollisionBroadphase)
            {
                bRunPointSweeps = HasPotentialWorldCollision(World, ActorXform, Params);
            }

            if (bRunPointSweeps)
            {
                const FCollisionShape SweepShape = FCollisionShape::MakeSphere(PointCollisionRadius);
                const float MinMoveDistanceSq =
                    MinWorldCollisionMoveDistance * MinWorldCollisionMoveDistance;

                for (FClothPoint& P : Points)
                {
                    if (!P.bActive || P.bPinned)
                    {
                        continue;
                    }

                    if (MinMoveDistanceSq > 0.0f &&
                        (P.Position - P.PrevPosition).SizeSquared() < MinMoveDistanceSq)
                    {
                        continue;
                    }

                    SolveWorldCollision(P, World, ActorXform, Params, SweepShape);
                }
            }
        }
    }

    if (bUseLocalBoxCollider)
    {
        for (FClothPoint& P : Points)
        {
            SolveLocalBoxCollision(P);
        }
    }

    if (bEnableClothToClothCollision)
    {
        SolveClothToClothCollisions();
    }
}

void AClothSimActor::SolveConstraintXPBD(FClothConstraint& Constraint, float Dt)
{
    if (Constraint.bBroken)
    {
        return;
    }

    FClothPoint& P1 = Points[Constraint.P1];
    FClothPoint& P2 = Points[Constraint.P2];

    if (!P1.bActive || !P2.bActive)
    {
        return;
    }

    const FVector Delta = P1.Position - P2.Position;
    const float Distance = Delta.Length();

    if (Distance <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const bool bSuppressTearFromClothContact =
        bPreventTearingFromClothToCloth &&
        ClothCollisionContactMask.IsValidIndex(Constraint.P1) &&
        ClothCollisionContactMask.IsValidIndex(Constraint.P2) &&
        (ClothCollisionContactMask[Constraint.P1] != 0 || ClothCollisionContactMask[Constraint.P2] != 0);

    if (bEnableTearing &&
        !bSuppressTearFromClothContact &&
        TearDistance > 0.0f &&
        Distance > Constraint.RestLength + TearDistance)
    {
        BreakConstraint(Constraint);
        return;
    }

    const float C = Distance - Constraint.RestLength;
    const FVector N = Delta / Distance;

    const float W1 = GetPointInverseMass(Constraint.P1);
    const float W2 = GetPointInverseMass(Constraint.P2);
    const float WSum = W1 + W2;

    if (WSum <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float WeightAntiStretchScale = 1.0f / FMath::Clamp(ClothWeightScale, 0.01f, 4.0f);
    const float GlobalAntiStretch = FMath::Clamp(HangingAntiStretch, 0.0f, 1.0f);
    const float AntiStretchComplianceScale = FMath::Lerp(1.0f, 0.05f, GlobalAntiStretch);
    float EffectiveCompliance = StretchCompliance * WeightAntiStretchScale * AntiStretchComplianceScale;
    if (bUseNearInextensibleMode)
    {
        const float NearModeScale = FMath::Lerp(1.0f, 0.01f, FMath::Clamp(NearInextensibleStrength, 0.0f, 1.0f));
        EffectiveCompliance *= NearModeScale;
    }
    const float Alpha = EffectiveCompliance / (Dt * Dt);
    const float Denominator = WSum + Alpha;

    if (Denominator <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    float DeltaLambda = (-C - Alpha * Constraint.Lambda) / Denominator;
    DeltaLambda *= Stiffness;

    Constraint.Lambda += DeltaLambda;

    const FVector Correction = DeltaLambda * N;

    if (W1 > KINDA_SMALL_NUMBER)
    {
        P1.Position += W1 * Correction;
    }

    if (W2 > KINDA_SMALL_NUMBER)
    {
        P2.Position -= W2 * Correction;
    }
}

void AClothSimActor::EnforceMaxStretch()
{
    if (MaxStretchRatio <= 1.0f)
    {
        return;
    }

    const float WeightAntiStretchScale = 1.0f / FMath::Clamp(ClothWeightScale, 0.01f, 4.0f);
    const float WeightedMaxStretchRatio = 1.0f + (MaxStretchRatio - 1.0f) * WeightAntiStretchScale;
    const float AntiStretch = FMath::Clamp(HangingAntiStretch, 0.0f, 1.0f);

    for (FClothConstraint& C : Constraints)
    {
        if (C.bBroken)
        {
            continue;
        }

        FClothPoint& P1 = Points[C.P1];
        FClothPoint& P2 = Points[C.P2];

        if (!P1.bActive || !P2.bActive)
        {
            continue;
        }

        const FVector Delta = P1.Position - P2.Position;
        const float Distance = Delta.Length();

        if (Distance <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        float EffectiveMaxStretchRatio = FMath::Lerp(WeightedMaxStretchRatio, 1.0f, AntiStretch);
        if (AntiStretch > KINDA_SMALL_NUMBER && (P1.bPinned != P2.bPinned))
        {
            // Extra stiffening around hanging anchors.
            EffectiveMaxStretchRatio = FMath::Lerp(EffectiveMaxStretchRatio, 1.0f, AntiStretch * 0.5f);
        }

        const float MaxAllowed = C.RestLength * EffectiveMaxStretchRatio;

        if (Distance <= MaxAllowed)
        {
            continue;
        }

        const bool bSuppressTearFromClothContact =
            bPreventTearingFromClothToCloth &&
            ClothCollisionContactMask.IsValidIndex(C.P1) &&
            ClothCollisionContactMask.IsValidIndex(C.P2) &&
            (ClothCollisionContactMask[C.P1] != 0 || ClothCollisionContactMask[C.P2] != 0);

        if (bEnableTearing &&
            !bSuppressTearFromClothContact &&
            TearDistance > 0.0f &&
            Distance > C.RestLength + TearDistance)
        {
            BreakConstraint(C);
            continue;
        }

        const FVector Dir = Delta / Distance;
        const float Excess = Distance - MaxAllowed;
        const float W1 = GetPointInverseMass(C.P1);
        const float W2 = GetPointInverseMass(C.P2);
        const float WSum = W1 + W2;
        if (WSum <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const FVector TotalCorrection = Dir * Excess;
        P1.Position -= TotalCorrection * (W1 / WSum);
        P2.Position += TotalCorrection * (W2 / WSum);
    }
}

void AClothSimActor::EnforceNearInextensibleStretch()
{
    if (!bUseNearInextensibleMode || NearInextensibleStrength <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    EnforceLengthRecovery(
        NearInextensiblePasses,
        NearInextensibleStrength,
        NearInextensibleStretchLimit
    );
}

void AClothSimActor::EnforceLengthRecovery(int32 Passes, float Strength, float StretchLimit)
{
    const int32 ClampedPasses = FMath::Clamp(Passes, 0, 32);
    const float ClampedStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
    const float BaseStretchLimit = FMath::Clamp(StretchLimit, 1.0f, 1.05f);
    if (ClampedPasses <= 0 || ClampedStrength <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float AntiStretch = FMath::Clamp(HangingAntiStretch, 0.0f, 1.0f);

    for (int32 Pass = 0; Pass < ClampedPasses; ++Pass)
    {
        for (FClothConstraint& C : Constraints)
        {
            if (C.bBroken)
            {
                continue;
            }

            FClothPoint& P1 = Points[C.P1];
            FClothPoint& P2 = Points[C.P2];

            if (!P1.bActive || !P2.bActive)
            {
                continue;
            }

            const FVector Delta = P1.Position - P2.Position;
            const float Distance = Delta.Length();
            if (Distance <= KINDA_SMALL_NUMBER)
            {
                continue;
            }

            float EffectiveStretchLimit = BaseStretchLimit;
            if (AntiStretch > KINDA_SMALL_NUMBER && (P1.bPinned != P2.bPinned))
            {
                // Extra tightening for hanging edges.
                EffectiveStretchLimit = FMath::Lerp(EffectiveStretchLimit, 1.0f, AntiStretch * 0.5f);
            }

            const float MaxAllowed = C.RestLength * EffectiveStretchLimit;
            if (Distance <= MaxAllowed)
            {
                continue;
            }

            const bool bSuppressTearFromClothContact =
                bPreventTearingFromClothToCloth &&
                ClothCollisionContactMask.IsValidIndex(C.P1) &&
                ClothCollisionContactMask.IsValidIndex(C.P2) &&
                (ClothCollisionContactMask[C.P1] != 0 || ClothCollisionContactMask[C.P2] != 0);

            if (bEnableTearing &&
                !bSuppressTearFromClothContact &&
                TearDistance > 0.0f &&
                Distance > C.RestLength + TearDistance)
            {
                BreakConstraint(C);
                continue;
            }

            const FVector Dir = Delta / Distance;
            const float Excess = (Distance - MaxAllowed) * ClampedStrength;
            const float W1 = GetPointInverseMass(C.P1);
            const float W2 = GetPointInverseMass(C.P2);
            const float WSum = W1 + W2;
            if (WSum <= KINDA_SMALL_NUMBER)
            {
                continue;
            }

            const FVector TotalCorrection = Dir * Excess;
            P1.Position -= TotalCorrection * (W1 / WSum);
            P2.Position += TotalCorrection * (W2 / WSum);
        }
    }
}

void AClothSimActor::SolveWorldCollision(
    FClothPoint& Point,
    UWorld* World,
    const FTransform& ActorXform,
    const FCollisionQueryParams& Params,
    const FCollisionShape& SweepShape
)
{
    if (!Point.bActive || Point.bPinned)
    {
        return;
    }

    if (!World)
    {
        return;
    }

    const FVector StartWS = ActorXform.TransformPosition(Point.PrevPosition);
    const FVector EndWS = ActorXform.TransformPosition(Point.Position);

    FHitResult Hit;
    const FQuat Rotation = FQuat::Identity;

    const bool bHit = World->SweepSingleByChannel(
        Hit,
        StartWS,
        EndWS,
        Rotation,
        WorldCollisionChannel,
        SweepShape,
        Params
    );

    if (!bHit)
    {
        return;
    }

    const FVector CorrectedWS = Hit.ImpactPoint + Hit.ImpactNormal * (PointCollisionRadius + SurfaceOffset);
    const FVector OldPosWS = EndWS;

    const FVector TangentVelocityWS =
        FVector::VectorPlaneProject(OldPosWS - StartWS, Hit.ImpactNormal) * 0.35f;

    Point.Position = ActorXform.InverseTransformPosition(CorrectedWS);
    Point.PrevPosition = ActorXform.InverseTransformPosition(CorrectedWS - TangentVelocityWS);

    if (bDrawWorldHits)
    {
        DebugWorldHits.Add(Hit);
    }
}

bool AClothSimActor::HasPotentialWorldCollision(
    UWorld* World,
    const FTransform& ActorXform,
    const FCollisionQueryParams& Params
) const
{
    if (!World)
    {
        return false;
    }

    FBox ClothBounds(EForceInit::ForceInit);

    for (const FClothPoint& P : Points)
    {
        if (!P.bActive || P.bPinned)
        {
            continue;
        }

        ClothBounds += ActorXform.TransformPosition(P.PrevPosition);
        ClothBounds += ActorXform.TransformPosition(P.Position);
    }

    if (ClothBounds.IsValid == 0)
    {
        return false;
    }

    const float Inflate = PointCollisionRadius + SurfaceOffset;
    const FVector RawExtent = ClothBounds.GetExtent() + FVector(Inflate);
    const FVector SafeExtent(
        FMath::Max(RawExtent.X, 1.0f),
        FMath::Max(RawExtent.Y, 1.0f),
        FMath::Max(RawExtent.Z, 1.0f)
    );

    return World->OverlapBlockingTestByChannel(
        ClothBounds.GetCenter(),
        FQuat::Identity,
        WorldCollisionChannel,
        FCollisionShape::MakeBox(SafeExtent),
        Params
    );
}

bool AClothSimActor::BuildClothBoundsWorld(const FTransform& ActorXform, FBox& OutBounds) const
{
    OutBounds = FBox(EForceInit::ForceInit);

    for (const FClothPoint& P : Points)
    {
        if (!P.bActive)
        {
            continue;
        }

        OutBounds += ActorXform.TransformPosition(P.Position);
    }

    return OutBounds.IsValid != 0;
}

void AClothSimActor::SolveClothToClothCollisions()
{
    if (!bEnableClothToClothCollision || Points.Num() <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const FTransform SelfXform = GetActorTransform();
    FBox SelfBounds;
    if (!BuildClothBoundsWorld(SelfXform, SelfBounds))
    {
        return;
    }

    const float SelfRadius = FMath::Max(0.1f, ClothToClothPointRadius);
    const float SelfOffset = FMath::Max(0.0f, ClothToClothSurfaceOffset);
    const FVector FallbackNormalWS = SelfXform.GetUnitAxis(EAxis::Z);

    for (TActorIterator<AClothSimActor> It(World); It; ++It)
    {
        AClothSimActor* Other = *It;
        if (!IsValid(Other) || Other == this || Other->IsActorBeingDestroyed())
        {
            continue;
        }

        if (!Other->bEnableClothToClothCollision || Other->Points.Num() <= 0)
        {
            continue;
        }

        const float OtherRadius = FMath::Max(0.1f, Other->ClothToClothPointRadius);
        const float OtherOffset = FMath::Max(0.0f, Other->ClothToClothSurfaceOffset);
        const float ContactDistance = SelfRadius + OtherRadius + FMath::Max(SelfOffset, OtherOffset);
        const float ContactDistanceSq = ContactDistance * ContactDistance;

        const FTransform OtherXform = Other->GetActorTransform();
        FBox OtherBounds;
        if (!Other->BuildClothBoundsWorld(OtherXform, OtherBounds))
        {
            continue;
        }

        const FBox ExpandedSelfBounds = SelfBounds.ExpandBy(ContactDistance);
        const FBox ExpandedOtherBounds = OtherBounds.ExpandBy(ContactDistance);
        if (!ExpandedSelfBounds.Intersect(ExpandedOtherBounds))
        {
            continue;
        }

        TArray<FVector> OtherWorldPoints;
        OtherWorldPoints.Reserve(Other->Points.Num());

        FBox OtherPointBounds(EForceInit::ForceInit);
        for (int32 i = 0; i < Other->Points.Num(); ++i)
        {
            const FClothPoint& OtherPoint = Other->Points[i];
            if (!OtherPoint.bActive)
            {
                continue;
            }

            const FVector OtherPointWS = OtherXform.TransformPosition(OtherPoint.Position);
            OtherWorldPoints.Add(OtherPointWS);
            OtherPointBounds += OtherPointWS;
        }

        if (OtherPointBounds.IsValid == 0 || OtherWorldPoints.Num() <= 0)
        {
            continue;
        }

        const FBox ExpandedOtherPointBounds = OtherPointBounds.ExpandBy(ContactDistance);

        for (int32 PointIdx = 0; PointIdx < Points.Num(); ++PointIdx)
        {
            FClothPoint& SelfPoint = Points[PointIdx];
            if (!SelfPoint.bActive || SelfPoint.bPinned)
            {
                continue;
            }

            const FVector SelfPointWS = SelfXform.TransformPosition(SelfPoint.Position);
            if (!ExpandedOtherPointBounds.IsInsideOrOn(SelfPointWS))
            {
                continue;
            }

            float BestDistSq = TNumericLimits<float>::Max();
            FVector BestDelta = FVector::ZeroVector;

            for (const FVector& OtherPointWS : OtherWorldPoints)
            {
                const FVector Delta = SelfPointWS - OtherPointWS;
                const float DistSq = Delta.SizeSquared();

                if (DistSq < BestDistSq)
                {
                    BestDistSq = DistSq;
                    BestDelta = Delta;
                }
            }

            if (BestDistSq >= ContactDistanceSq)
            {
                continue;
            }

            const float Dist = FMath::Sqrt(FMath::Max(BestDistSq, KINDA_SMALL_NUMBER));
            const FVector NormalWS = Dist > KINDA_SMALL_NUMBER ? (BestDelta / Dist) : FallbackNormalWS;
            const float Penetration = ContactDistance - Dist;
            const FVector CorrectedWS = SelfPointWS + NormalWS * Penetration;

            const FVector PrevWS = SelfXform.TransformPosition(SelfPoint.PrevPosition);
            const FVector VelocityWS = CorrectedWS - PrevWS;
            const FVector NormalVelWS = FVector::DotProduct(VelocityWS, NormalWS) * NormalWS;
            const FVector TangentVelWS = (VelocityWS - NormalVelWS) * (1.0f - ClothToClothFriction);

            SelfPoint.Position = SelfXform.InverseTransformPosition(CorrectedWS);
            SelfPoint.PrevPosition = SelfXform.InverseTransformPosition(CorrectedWS - TangentVelWS);

            if (bPreventTearingFromClothToCloth && ClothCollisionContactMask.IsValidIndex(PointIdx))
            {
                ClothCollisionContactMask[PointIdx] = 1;
            }
        }
    }
}

void AClothSimActor::SolveLocalBoxCollision(FClothPoint& Point)
{
    if (!Point.bActive || Point.bPinned)
    {
        return;
    }

    const FVector LocalPoint = Point.Position;
    const FVector LocalMin = BoxCenter - BoxExtent;
    const FVector LocalMax = BoxCenter + BoxExtent;

    const bool bInside =
        (LocalPoint.X > LocalMin.X && LocalPoint.X < LocalMax.X) &&
        (LocalPoint.Y > LocalMin.Y && LocalPoint.Y < LocalMax.Y) &&
        (LocalPoint.Z > LocalMin.Z && LocalPoint.Z < LocalMax.Z);

    if (!bInside)
    {
        return;
    }

    const float DistToMinX = FMath::Abs(LocalPoint.X - LocalMin.X);
    const float DistToMaxX = FMath::Abs(LocalMax.X - LocalPoint.X);
    const float DistToMinY = FMath::Abs(LocalPoint.Y - LocalMin.Y);
    const float DistToMaxY = FMath::Abs(LocalMax.Y - LocalPoint.Y);
    const float DistToMinZ = FMath::Abs(LocalPoint.Z - LocalMin.Z);
    const float DistToMaxZ = FMath::Abs(LocalMax.Z - LocalPoint.Z);

    float MinDist = DistToMinX;
    int32 Face = 0;

    if (DistToMaxX < MinDist) { MinDist = DistToMaxX; Face = 1; }
    if (DistToMinY < MinDist) { MinDist = DistToMinY; Face = 2; }
    if (DistToMaxY < MinDist) { MinDist = DistToMaxY; Face = 3; }
    if (DistToMinZ < MinDist) { MinDist = DistToMinZ; Face = 4; }
    if (DistToMaxZ < MinDist) { MinDist = DistToMaxZ; Face = 5; }

    FVector CorrectedLocal = LocalPoint;
    const float Epsilon = 0.5f;

    switch (Face)
    {
    case 0: CorrectedLocal.X = LocalMin.X - Epsilon; break;
    case 1: CorrectedLocal.X = LocalMax.X + Epsilon; break;
    case 2: CorrectedLocal.Y = LocalMin.Y - Epsilon; break;
    case 3: CorrectedLocal.Y = LocalMax.Y + Epsilon; break;
    case 4: CorrectedLocal.Z = LocalMin.Z - Epsilon; break;
    case 5: CorrectedLocal.Z = LocalMax.Z + Epsilon; break;
    default: break;
    }

    Point.Position = CorrectedLocal;
    Point.PrevPosition = CorrectedLocal;
}

void AClothSimActor::RePinPoints()
{
    for (FClothPoint& P : Points)
    {
        if (!P.bActive)
        {
            continue;
        }

        if (P.bPinned)
        {
            P.Position = P.PinPosition;
            P.PrevPosition = P.PinPosition;
            P.Acceleration = FVector::ZeroVector;
            continue;
        }

        const float SoftPinAlpha = FMath::Clamp(P.PinWeight * SoftPinStiffness, 0.0f, 1.0f);
        if (SoftPinAlpha > 0.0f)
        {
            P.Position = FMath::Lerp(P.Position, P.PinPosition, SoftPinAlpha);
            P.PrevPosition = FMath::Lerp(P.PrevPosition, P.PinPosition, SoftPinAlpha);
        }
    }
}

void AClothSimActor::ApplyDamageAtPoint(const FVector& WorldPoint, float Radius)
{
    const float UseRadius = Radius > 0.0f ? Radius : DamageRadius;
    const FVector LocalPoint = GetActorTransform().InverseTransformPosition(WorldPoint);

    for (FClothConstraint& C : Constraints)
    {
        if (C.bBroken)
        {
            continue;
        }

        if (!Points.IsValidIndex(C.P1) || !Points.IsValidIndex(C.P2))
        {
            continue;
        }

        const FVector A = Points[C.P1].Position;
        const FVector B = Points[C.P2].Position;
        const float Dist = DistancePointToSegment(LocalPoint, A, B);

        if (Dist <= UseRadius)
        {
            BreakConstraint(C);
        }
    }

    UpdateRenderMesh();
}

void AClothSimActor::ApplyDamageFromLineTrace(const FVector& TraceStart, const FVector& TraceEnd, float Radius)
{
    const float UseRadius = Radius > 0.0f ? Radius : DamageRadius;

    const FTransform ActorXform = GetActorTransform();
    const FVector LocalStart = ActorXform.InverseTransformPosition(TraceStart);
    const FVector LocalEnd = ActorXform.InverseTransformPosition(TraceEnd);

    for (FClothConstraint& C : Constraints)
    {
        if (C.bBroken)
        {
            continue;
        }

        if (!Points.IsValidIndex(C.P1) || !Points.IsValidIndex(C.P2))
        {
            continue;
        }

        const FVector A = Points[C.P1].Position;
        const FVector B = Points[C.P2].Position;
        const float Dist = DistanceSegmentToSegment(LocalStart, LocalEnd, A, B);

        if (Dist <= UseRadius)
        {
            BreakConstraint(C);
        }
    }

    UpdateRenderMesh();
}

bool AClothSimActor::IsConstraintIntact(int32 A, int32 B) const
{
    if (A == B)
    {
        return false;
    }

    const int32* FoundIndex = ConstraintLookup.Find(MakeEdgeKey(A, B));
    if (!FoundIndex || !Constraints.IsValidIndex(*FoundIndex))
    {
        return false;
    }

    return !Constraints[*FoundIndex].bBroken;
}

bool AClothSimActor::IsTriangleValid(int32 A, int32 B, int32 C) const
{
    if (!Points.IsValidIndex(A) || !Points.IsValidIndex(B) || !Points.IsValidIndex(C))
    {
        return false;
    }

    if (!Points[A].bActive || !Points[B].bActive || !Points[C].bActive)
    {
        return false;
    }

    return IsConstraintIntact(A, B) &&
        IsConstraintIntact(B, C) &&
        IsConstraintIntact(C, A);
}

bool AClothSimActor::AddTriangleIfValid(TArray<int32>& Triangles, int32 A, int32 B, int32 C) const
{
    if (IsTriangleValid(A, B, C))
    {
        Triangles.Add(A);
        Triangles.Add(B);
        Triangles.Add(C);
        return true;
    }

    return false;
}

void AClothSimActor::RebuildTriangleIndexBuffer()
{
    RenderTriangles.Reset();
    RenderShadingTriangles.Reset();

    if (bUsingSourceMeshTopology && BaseTriangles.Num() >= 3)
    {
        RenderTriangles.Reserve(BaseTriangles.Num() * (bRenderDoubleSided ? 2 : 1));
        RenderShadingTriangles.Reserve(BaseTriangles.Num());

        for (int32 T = 0; T + 2 < BaseTriangles.Num(); T += 3)
        {
            const int32 I0 = BaseTriangles[T];
            const int32 I1 = BaseTriangles[T + 1];
            const int32 I2 = BaseTriangles[T + 2];

            if (AddTriangleIfValid(RenderTriangles, I0, I1, I2))
            {
                RenderShadingTriangles.Add(I0);
                RenderShadingTriangles.Add(I1);
                RenderShadingTriangles.Add(I2);
            }

            if (bRenderDoubleSided)
            {
                AddTriangleIfValid(RenderTriangles, I2, I1, I0);
            }
        }
    }
    else
    {
        const int32 EstimatedTriCount = ClothWidth * ClothHeight * (bRenderDoubleSided ? 12 : 6);
        RenderTriangles.Reserve(EstimatedTriCount);
        RenderShadingTriangles.Reserve(ClothWidth * ClothHeight * 6);

        for (int32 Y = 0; Y < ClothHeight; ++Y)
        {
            for (int32 X = 0; X < ClothWidth; ++X)
            {
                const int32 I00 = GetPointIndex(X, Y);
                const int32 I10 = GetPointIndex(X + 1, Y);
                const int32 I01 = GetPointIndex(X, Y + 1);
                const int32 I11 = GetPointIndex(X + 1, Y + 1);

                if (AddTriangleIfValid(RenderTriangles, I00, I10, I11))
                {
                    RenderShadingTriangles.Add(I00);
                    RenderShadingTriangles.Add(I10);
                    RenderShadingTriangles.Add(I11);
                }

                if (AddTriangleIfValid(RenderTriangles, I00, I11, I01))
                {
                    RenderShadingTriangles.Add(I00);
                    RenderShadingTriangles.Add(I11);
                    RenderShadingTriangles.Add(I01);
                }

                if (bRenderDoubleSided)
                {
                    AddTriangleIfValid(RenderTriangles, I11, I10, I00);
                    AddTriangleIfValid(RenderTriangles, I01, I11, I00);
                }
            }
        }
    }

    bTopologyDirty = false;
}

void AClothSimActor::RecalculateNormalsAndTangents()
{
    const int32 VertexCount = RenderVertices.Num();
    if (VertexCount <= 0)
    {
        return;
    }

    const TArray<int32>& TrianglesForShading =
        RenderShadingTriangles.Num() > 0 ? RenderShadingTriangles : RenderTriangles;

    for (int32 i = 0; i < VertexCount; ++i)
    {
        RenderNormals[i] = FVector::ZeroVector;
    }

    for (int32 T = 0; T < TrianglesForShading.Num(); T += 3)
    {
        const int32 IA = TrianglesForShading[T];
        const int32 IB = TrianglesForShading[T + 1];
        const int32 IC = TrianglesForShading[T + 2];

        if (!RenderVertices.IsValidIndex(IA) ||
            !RenderVertices.IsValidIndex(IB) ||
            !RenderVertices.IsValidIndex(IC))
        {
            continue;
        }

        const FVector& A = RenderVertices[IA];
        const FVector& B = RenderVertices[IB];
        const FVector& C = RenderVertices[IC];

        const FVector FaceNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
        RenderNormals[IA] += FaceNormal;
        RenderNormals[IB] += FaceNormal;
        RenderNormals[IC] += FaceNormal;
    }

    for (int32 I = 0; I < VertexCount; ++I)
    {
        RenderNormals[I] = RenderNormals[I].GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);
    }

    TArray<FVector> TangentAccum;
    TangentAccum.SetNumZeroed(VertexCount);

    for (int32 T = 0; T < TrianglesForShading.Num(); T += 3)
    {
        const int32 IA = TrianglesForShading[T];
        const int32 IB = TrianglesForShading[T + 1];
        const int32 IC = TrianglesForShading[T + 2];

        if (!RenderVertices.IsValidIndex(IA) ||
            !RenderVertices.IsValidIndex(IB) ||
            !RenderVertices.IsValidIndex(IC) ||
            !RenderUV0.IsValidIndex(IA) ||
            !RenderUV0.IsValidIndex(IB) ||
            !RenderUV0.IsValidIndex(IC))
        {
            continue;
        }

        const FVector& A = RenderVertices[IA];
        const FVector& B = RenderVertices[IB];
        const FVector& C = RenderVertices[IC];
        const FVector2D& UVa = RenderUV0[IA];
        const FVector2D& UVb = RenderUV0[IB];
        const FVector2D& UVc = RenderUV0[IC];

        const FVector Edge1 = B - A;
        const FVector Edge2 = C - A;
        const FVector2D Duv1 = UVb - UVa;
        const FVector2D Duv2 = UVc - UVa;
        const float Det = Duv1.X * Duv2.Y - Duv2.X * Duv1.Y;

        FVector TangentDir = FVector::ZeroVector;
        if (FMath::Abs(Det) > KINDA_SMALL_NUMBER)
        {
            const float InvDet = 1.0f / Det;
            TangentDir = (Edge1 * Duv2.Y - Edge2 * Duv1.Y) * InvDet;
        }
        else
        {
            TangentDir = Edge1;
        }

        TangentAccum[IA] += TangentDir;
        TangentAccum[IB] += TangentDir;
        TangentAccum[IC] += TangentDir;
    }

    for (int32 I = 0; I < VertexCount; ++I)
    {
        FVector TangentDir = TangentAccum[I];
        if (TangentDir.IsNearlyZero())
        {
            TangentDir = FVector::CrossProduct(FVector::UpVector, RenderNormals[I]);
            if (TangentDir.IsNearlyZero())
            {
                TangentDir = FVector::CrossProduct(FVector::ForwardVector, RenderNormals[I]);
            }
        }

        TangentDir = (TangentDir - RenderNormals[I] * FVector::DotProduct(RenderNormals[I], TangentDir)).GetSafeNormal();
        if (TangentDir.IsNearlyZero())
        {
            TangentDir = FVector::ForwardVector;
        }

        RenderTangents[I] = FProcMeshTangent(TangentDir, false);
    }
}

void AClothSimActor::UpdateRenderMesh()
{
    if (!ClothMesh)
    {
        return;
    }

    if (!bRenderMesh)
    {
        if (bMeshSectionCreated)
        {
            ClothMesh->ClearAllMeshSections();
            bMeshSectionCreated = false;
        }
        return;
    }

    const int32 VertexCount = Points.Num();
    if (VertexCount <= 0)
    {
        if (bMeshSectionCreated)
        {
            ClothMesh->ClearAllMeshSections();
            bMeshSectionCreated = false;
        }
        return;
    }

    if (RenderVertices.Num() != VertexCount)
    {
        InitializeRenderBuffers();
        bTopologyDirty = true;
        bMeshSectionCreated = false;
        bRenderVerticesDirty = true;
    }

    const bool bHadTopologyDirty = bTopologyDirty;
    const bool bNeedGeometryUpdate =
        bRenderVerticesDirty || bHadTopologyDirty || !bMeshSectionCreated;

    if (bRenderVerticesDirty)
    {
        for (int32 i = 0; i < VertexCount; ++i)
        {
            RenderVertices[i] = Points[i].Position;
        }
    }

    if (bTopologyDirty)
    {
        RebuildTriangleIndexBuffer();
    }

    if (bNeedGeometryUpdate)
    {
        ApplyTearEdgeSmoothing();
        RecalculateNormalsAndTangents();
    }

    if (!bMeshSectionCreated || bHadTopologyDirty)
    {
        if (bMeshSectionCreated)
        {
            ClothMesh->ClearAllMeshSections();
        }

        ClothMesh->CreateMeshSection_LinearColor(
            0,
            RenderVertices,
            RenderTriangles,
            RenderNormals,
            RenderUV0,
            RenderVertexColors,
            RenderTangents,
            bCreateMeshCollision
        );

        bMeshSectionCreated = true;
    }
    else if (bNeedGeometryUpdate)
    {
        ClothMesh->UpdateMeshSection_LinearColor(
            0,
            RenderVertices,
            RenderNormals,
            RenderUV0,
            RenderVertexColors,
            RenderTangents
        );
    }

    if (ClothMaterial && ClothMesh->GetMaterial(0) != ClothMaterial)
    {
        ClothMesh->SetMaterial(0, ClothMaterial);
    }

    if (!ClothMesh->IsVisible())
    {
        ClothMesh->SetVisibility(true);
    }

    const ECollisionEnabled::Type DesiredCollisionMode =
        bCreateMeshCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
    if (ClothMesh->GetCollisionEnabled() != DesiredCollisionMode)
    {
        ClothMesh->SetCollisionEnabled(DesiredCollisionMode);
    }

    DrawTearStrands();
    bRenderVerticesDirty = false;
}

void AClothSimActor::ApplyTearEdgeSmoothing()
{
    if (!bSmoothTearEdges ||
        TearEdgeSmoothStrength <= KINDA_SMALL_NUMBER ||
        Constraints.Num() <= 0 ||
        RenderVertices.Num() != Points.Num())
    {
        return;
    }

    const int32 VertexCount = RenderVertices.Num();
    TArray<uint8> BoundaryMask;
    BoundaryMask.SetNumZeroed(VertexCount);

    bool bHasBoundary = false;
    for (const FClothConstraint& C : Constraints)
    {
        if (!C.bBroken)
        {
            continue;
        }

        if (Points.IsValidIndex(C.P1) && Points[C.P1].bActive)
        {
            BoundaryMask[C.P1] = 1;
            bHasBoundary = true;
        }
        if (Points.IsValidIndex(C.P2) && Points[C.P2].bActive)
        {
            BoundaryMask[C.P2] = 1;
            bHasBoundary = true;
        }
    }

    if (!bHasBoundary)
    {
        return;
    }

    const int32 Iterations = FMath::Clamp(TearEdgeSmoothIterations, 1, 6);
    const float SmoothAlpha = FMath::Clamp(TearEdgeSmoothStrength, 0.0f, 1.0f);

    TArray<FVector> NeighborSum;
    TArray<int32> NeighborCount;
    TArray<FVector> Smoothed;
    NeighborSum.SetNumZeroed(VertexCount);
    NeighborCount.SetNumZeroed(VertexCount);
    Smoothed.SetNumZeroed(VertexCount);

    for (int32 Iter = 0; Iter < Iterations; ++Iter)
    {
        for (int32 i = 0; i < VertexCount; ++i)
        {
            NeighborSum[i] = FVector::ZeroVector;
            NeighborCount[i] = 0;
            Smoothed[i] = RenderVertices[i];
        }

        for (const FClothConstraint& C : Constraints)
        {
            if (C.bBroken ||
                !Points.IsValidIndex(C.P1) ||
                !Points.IsValidIndex(C.P2) ||
                !Points[C.P1].bActive ||
                !Points[C.P2].bActive)
            {
                continue;
            }

            NeighborSum[C.P1] += RenderVertices[C.P2];
            NeighborSum[C.P2] += RenderVertices[C.P1];
            NeighborCount[C.P1] += 1;
            NeighborCount[C.P2] += 1;
        }

        for (int32 i = 0; i < VertexCount; ++i)
        {
            if (BoundaryMask[i] == 0 || !Points[i].bActive || Points[i].bPinned || NeighborCount[i] < 2)
            {
                continue;
            }

            const FVector Avg = NeighborSum[i] / static_cast<float>(NeighborCount[i]);
            Smoothed[i] = FMath::Lerp(RenderVertices[i], Avg, SmoothAlpha);
        }

        RenderVertices = Smoothed;
    }
}

void AClothSimActor::DrawTearStrands()
{
    if (!ClothMesh)
    {
        return;
    }

    constexpr int32 StrandSectionIndex = 1;

    UWorld* World = GetWorld();
    const bool bIsGameWorld = World ? World->IsGameWorld() : true;
    const bool bAllowedByWorldType =
        (bIsGameWorld && bShowTearStrandsInGame) ||
        (!bIsGameWorld && bShowTearStrandsInEditor);

    if (!bRenderTearStrands ||
        TearStrandThickness <= KINDA_SMALL_NUMBER ||
        !bAllowedByWorldType ||
        Constraints.Num() <= 0 ||
        RenderUV0.Num() != Points.Num())
    {
        ClothMesh->ClearMeshSection(StrandSectionIndex);
        return;
    }

    const int32 StrandsPerEdge = FMath::Clamp(TearStrandCountPerEdge, 1, 8);
    const float Thickness = FMath::Max(0.0f, TearStrandThickness);
    const float HalfWidth = FMath::Max(0.02f, Thickness * 0.5f);
    const float Spread = FMath::Max(0.0f, TearStrandSpread);
    const float Irregularity = FMath::Clamp(TearStrandIrregularity, 0.0f, 1.0f);
    const float StrandMaxStretchRatio = FMath::Max(1.0f, TearStrandMaxStretchRatio);
    const int32 MaxStrands = FMath::Clamp(MaxRenderedTearStrands, 1, 4096);
    const float MinWidthScale = FMath::Lerp(0.9f, 0.18f, Irregularity);
    const float StrandDropChance = 0.18f * Irregularity;
    const bool bDoubleSidedStrands = bRenderDoubleSided;
    const FLinearColor StrandVertexColor(TearStrandColor);

    TArray<FVector> StrandVertices;
    TArray<int32> StrandTriangles;
    TArray<FVector> StrandNormals;
    TArray<FVector2D> StrandUV0;
    TArray<FLinearColor> StrandColors;
    TArray<FProcMeshTangent> StrandTangents;

    StrandVertices.Reserve(MaxStrands * 4);
    StrandTriangles.Reserve(MaxStrands * (bDoubleSidedStrands ? 12 : 6));
    StrandNormals.Reserve(MaxStrands * 4);
    StrandUV0.Reserve(MaxStrands * 4);
    StrandColors.Reserve(MaxStrands * 4);
    StrandTangents.Reserve(MaxStrands * 4);

    TArray<int32> IntactNeighborCounts;
    IntactNeighborCounts.SetNumZeroed(Points.Num());
    for (const FClothConstraint& C : Constraints)
    {
        if (C.bBroken ||
            !Points.IsValidIndex(C.P1) ||
            !Points.IsValidIndex(C.P2) ||
            !Points[C.P1].bActive ||
            !Points[C.P2].bActive)
        {
            continue;
        }

        IntactNeighborCounts[C.P1] += 1;
        IntactNeighborCounts[C.P2] += 1;
    }

    int32 BuiltStrands = 0;

    for (const FClothConstraint& C : Constraints)
    {
        if (!C.bBroken ||
            !Points.IsValidIndex(C.P1) ||
            !Points.IsValidIndex(C.P2) ||
            !Points[C.P1].bActive ||
            !Points[C.P2].bActive)
        {
            continue;
        }
        if (!IntactNeighborCounts.IsValidIndex(C.P1) ||
            !IntactNeighborCounts.IsValidIndex(C.P2) ||
            IntactNeighborCounts[C.P1] <= 0 ||
            IntactNeighborCounts[C.P2] <= 0)
        {
            continue;
        }

        const FVector A = Points[C.P1].Position;
        const FVector B = Points[C.P2].Position;
        const FVector Delta = B - A;
        const float Distance = Delta.Length();
        const float MinVisibleDistance = FMath::Max(C.RestLength * 0.18f, 0.08f);
        const float MaxVisibleDistance = FMath::Max(C.RestLength * StrandMaxStretchRatio, C.RestLength + 0.01f);
        if (Distance <= MinVisibleDistance || Distance > MaxVisibleDistance)
        {
            continue;
        }

        const FVector Dir = Delta / Distance;
        const FVector RefAxis = FMath::Abs(FVector::DotProduct(Dir, FVector::UpVector)) > 0.92f
            ? FVector::RightVector
            : FVector::UpVector;
        const FVector Side = FVector::CrossProduct(Dir, RefAxis).GetSafeNormal();
        FVector Up = FVector::CrossProduct(Dir, Side).GetSafeNormal();
        if (Up.IsNearlyZero())
        {
            Up = FVector::UpVector;
        }

        const FVector2D UVA = RenderUV0[C.P1];
        const FVector2D UVB = RenderUV0[C.P2];

        const uint32 Seed = HashCombine(GetTypeHash(C.P1), GetTypeHash(C.P2));
        FRandomStream Rand(static_cast<int32>(Seed));

        int32 EdgeStrands = StrandsPerEdge;
        if (Irregularity > KINDA_SMALL_NUMBER && StrandsPerEdge > 1)
        {
            const int32 MinEdgeStrands = FMath::Max(
                1,
                FMath::FloorToInt(FMath::Lerp(static_cast<float>(StrandsPerEdge), 1.0f, Irregularity * 0.7f))
            );
            EdgeStrands = Rand.RandRange(MinEdgeStrands, StrandsPerEdge);
        }

        for (int32 StrandIdx = 0; StrandIdx < EdgeStrands; ++StrandIdx)
        {
            if (BuiltStrands >= MaxStrands)
            {
                break;
            }

            if (Rand.FRand() < StrandDropChance)
            {
                continue;
            }

            const float TwistDegrees = Rand.FRandRange(-16.0f, 16.0f) * Irregularity;
            const FVector StrandSide = FQuat(Dir, FMath::DegreesToRadians(TwistDegrees))
                .RotateVector(Side)
                .GetSafeNormal();
            if (StrandSide.IsNearlyZero())
            {
                continue;
            }

            const float WidthScaleA = Rand.FRandRange(MinWidthScale, 1.0f);
            const float TaperScale = FMath::Lerp(1.0f, Rand.FRandRange(0.25f, 1.0f), Irregularity);
            const float WidthScaleB = FMath::Clamp(WidthScaleA * TaperScale, 0.08f, 1.0f);
            const float HalfWidthA = HalfWidth * WidthScaleA;
            const float HalfWidthB = HalfWidth * WidthScaleB;

            // Keep strand attached: midpoint at A/B is fixed on the tear edge.
            const FVector V0 = A - StrandSide * HalfWidthA;
            const FVector V1 = A + StrandSide * HalfWidthA;
            const FVector V2 = B - StrandSide * HalfWidthB;
            const FVector V3 = B + StrandSide * HalfWidthB;

            FVector Normal = FVector::CrossProduct(V2 - V0, V1 - V0).GetSafeNormal();
            if (Normal.IsNearlyZero())
            {
                Normal = Up;
            }

            const float UvJitter = (Rand.FRand() - 0.5f) * 0.0016f * (0.25f + Irregularity);
            const FVector2D UvSideOffset(UvJitter, 0.0f);
            const FVector2D UvWidthOffset(FMath::Lerp(0.0008f, 0.00035f, Irregularity), 0.0f);

            const float FiberShade = FMath::Lerp(1.0f, Rand.FRandRange(0.82f, 1.05f), Irregularity);
            const FLinearColor FiberColor = StrandVertexColor * FiberShade;

            const int32 BaseIndex = StrandVertices.Num();
            StrandVertices.Add(V0);
            StrandVertices.Add(V1);
            StrandVertices.Add(V2);
            StrandVertices.Add(V3);

            StrandNormals.Add(Normal);
            StrandNormals.Add(Normal);
            StrandNormals.Add(Normal);
            StrandNormals.Add(Normal);

            StrandUV0.Add(UVA + UvSideOffset - UvWidthOffset);
            StrandUV0.Add(UVA + UvSideOffset + UvWidthOffset);
            StrandUV0.Add(UVB + UvSideOffset - UvWidthOffset);
            StrandUV0.Add(UVB + UvSideOffset + UvWidthOffset);

            StrandColors.Add(FiberColor);
            StrandColors.Add(FiberColor);
            StrandColors.Add(FiberColor);
            StrandColors.Add(FiberColor);

            const FProcMeshTangent StrandTangent(Dir, false);
            StrandTangents.Add(StrandTangent);
            StrandTangents.Add(StrandTangent);
            StrandTangents.Add(StrandTangent);
            StrandTangents.Add(StrandTangent);

            StrandTriangles.Add(BaseIndex + 0);
            StrandTriangles.Add(BaseIndex + 2);
            StrandTriangles.Add(BaseIndex + 3);
            StrandTriangles.Add(BaseIndex + 0);
            StrandTriangles.Add(BaseIndex + 3);
            StrandTriangles.Add(BaseIndex + 1);

            if (bDoubleSidedStrands)
            {
                StrandTriangles.Add(BaseIndex + 3);
                StrandTriangles.Add(BaseIndex + 2);
                StrandTriangles.Add(BaseIndex + 0);
                StrandTriangles.Add(BaseIndex + 1);
                StrandTriangles.Add(BaseIndex + 3);
                StrandTriangles.Add(BaseIndex + 0);
            }

            ++BuiltStrands;
        }

        if (BuiltStrands >= MaxStrands)
        {
            break;
        }
    }

    if (StrandTriangles.Num() <= 0 || StrandVertices.Num() <= 0)
    {
        ClothMesh->ClearMeshSection(StrandSectionIndex);
        return;
    }

    ClothMesh->CreateMeshSection_LinearColor(
        StrandSectionIndex,
        StrandVertices,
        StrandTriangles,
        StrandNormals,
        StrandUV0,
        StrandColors,
        StrandTangents,
        false
    );

    UMaterialInterface* StrandMaterial = ClothMaterial ? ClothMaterial : ClothMesh->GetMaterial(0);
    if (StrandMaterial && ClothMesh->GetMaterial(StrandSectionIndex) != StrandMaterial)
    {
        ClothMesh->SetMaterial(StrandSectionIndex, StrandMaterial);
    }
}

float AClothSimActor::DistancePointToSegment(const FVector& P, const FVector& A, const FVector& B) const
{
    const FVector AB = B - A;
    const float LengthSq = AB.SizeSquared();

    if (LengthSq <= KINDA_SMALL_NUMBER)
    {
        return FVector::Dist(P, A);
    }

    const float T = FMath::Clamp(FVector::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f);
    const FVector Closest = A + AB * T;
    return FVector::Dist(P, Closest);
}

float AClothSimActor::DistanceSegmentToSegment(const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1) const
{
    const FVector U = A1 - A0;
    const FVector V = B1 - B0;
    const FVector W = A0 - B0;

    const float a = FVector::DotProduct(U, U);
    const float b = FVector::DotProduct(U, V);
    const float c = FVector::DotProduct(V, V);
    const float d = FVector::DotProduct(U, W);
    const float e = FVector::DotProduct(V, W);
    const float D = a * c - b * b;

    float sc, sN, sD = D;
    float tc, tN, tD = D;

    if (D < KINDA_SMALL_NUMBER)
    {
        sN = 0.0f;
        sD = 1.0f;
        tN = e;
        tD = c;
    }
    else
    {
        sN = (b * e - c * d);
        tN = (a * e - b * d);

        if (sN < 0.0f)
        {
            sN = 0.0f;
            tN = e;
            tD = c;
        }
        else if (sN > sD)
        {
            sN = sD;
            tN = e + b;
            tD = c;
        }
    }

    if (tN < 0.0f)
    {
        tN = 0.0f;

        if (-d < 0.0f)
        {
            sN = 0.0f;
        }
        else if (-d > a)
        {
            sN = sD;
        }
        else
        {
            sN = -d;
            sD = a;
        }
    }
    else if (tN > tD)
    {
        tN = tD;

        if ((-d + b) < 0.0f)
        {
            sN = 0.0f;
        }
        else if ((-d + b) > a)
        {
            sN = sD;
        }
        else
        {
            sN = (-d + b);
            sD = a;
        }
    }

    sc = (FMath::Abs(sN) < KINDA_SMALL_NUMBER ? 0.0f : sN / sD);
    tc = (FMath::Abs(tN) < KINDA_SMALL_NUMBER ? 0.0f : tN / tD);

    const FVector dP = W + (U * sc) - (V * tc);
    return dP.Length();
}

void AClothSimActor::DebugDraw() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const bool bIsGameWorld = World->IsGameWorld();
    const bool bAllowPinDebug = !bIsGameWorld || bShowPinDebugInGame;
    const bool bNeedWorldHitDraw = bDrawWorldHits && DebugWorldHits.Num() > 0;
    const bool bNeedPinRegionDraw = bAllowPinDebug && bUsePinRegion && bDrawPinRegionGizmo;
    const bool bNeedDebugDraw =
        bDrawPoints ||
        bDrawConstraints ||
        bDrawBrokenConstraints ||
        (bUseLocalBoxCollider && bDrawBox) ||
        bNeedPinRegionDraw ||
        bNeedWorldHitDraw;

    if (!bNeedDebugDraw)
    {
        return;
    }

    const FTransform ActorXform = GetActorTransform();

    if (bUseLocalBoxCollider && bDrawBox)
    {
        DrawDebugBox(
            World,
            ActorXform.TransformPosition(BoxCenter),
            BoxExtent,
            GetActorQuat(),
            BoxColor,
            false,
            0.0f,
            0,
            2.0f
        );
    }

    if (bNeedPinRegionDraw)
    {
        if (PinRegionType == EPinRegionType::Box)
        {
            DrawDebugBox(
                World,
                ActorXform.TransformPosition(PinRegionCenter),
                PinRegionExtent,
                GetActorQuat(),
                PinRegionColor,
                false,
                0.0f,
                0,
                2.0f
            );
        }
        else
        {
            DrawDebugSphere(
                World,
                ActorXform.TransformPosition(PinRegionCenter),
                PinRegionRadius,
                24,
                PinRegionColor,
                false,
                0.0f,
                0,
                1.5f
            );
        }
    }

    if (bDrawConstraints || bDrawBrokenConstraints)
    {
        for (const FClothConstraint& C : Constraints)
        {
            if (!Points.IsValidIndex(C.P1) || !Points.IsValidIndex(C.P2))
            {
                continue;
            }

            if (!Points[C.P1].bActive || !Points[C.P2].bActive)
            {
                continue;
            }

            const FVector P1WS = ActorXform.TransformPosition(Points[C.P1].Position);
            const FVector P2WS = ActorXform.TransformPosition(Points[C.P2].Position);

            if (C.bBroken)
            {
                if (bDrawBrokenConstraints)
                {
                    DrawDebugLine(
                        World,
                        P1WS,
                        P2WS,
                        BrokenConstraintColor,
                        false,
                        0.0f,
                        0,
                        0.75f
                    );
                }
            }
            else if (bDrawConstraints)
            {
                DrawDebugLine(
                    World,
                    P1WS,
                    P2WS,
                    ConstraintColor,
                    false,
                    0.0f,
                    0,
                    1.0f
                );
            }
        }
    }

    if (bDrawPoints)
    {
        for (const FClothPoint& P : Points)
        {
            if (!P.bActive)
            {
                continue;
            }

            const FVector PWS = ActorXform.TransformPosition(P.Position);

            DrawDebugSphere(
                World,
                PWS,
                PointDrawSize,
                8,
                P.bPinned ? PinnedPointColor : PointColor,
                false,
                0.0f,
                0,
                1.0f
            );
        }
    }

    if (bNeedWorldHitDraw)
    {
        for (const FHitResult& Hit : DebugWorldHits)
        {
            DrawDebugPoint(World, Hit.ImpactPoint, 8.0f, FColor::Green, false, 0.0f);
            DrawDebugLine(
                World,
                Hit.ImpactPoint,
                Hit.ImpactPoint + Hit.ImpactNormal * 18.0f,
                FColor::Green,
                false,
                0.0f,
                0,
                1.5f
            );
        }
    }
}
