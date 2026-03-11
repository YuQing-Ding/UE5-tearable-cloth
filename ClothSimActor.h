#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "ClothSimActor.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UWorld;
struct FCollisionQueryParams;
struct FCollisionShape;

USTRUCT(BlueprintType)
struct FClothPoint
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Position = FVector::ZeroVector;      // Local space

    UPROPERTY()
    FVector PrevPosition = FVector::ZeroVector;  // Local space

    UPROPERTY()
    FVector Acceleration = FVector::ZeroVector;  // Local space

    UPROPERTY()
    bool bActive = true;

    UPROPERTY()
    float PinWeight = 0.0f;

    UPROPERTY()
    bool bPinned = false;

    UPROPERTY()
    FVector PinPosition = FVector::ZeroVector;   // Local space
};

USTRUCT(BlueprintType)
struct FClothConstraint
{
    GENERATED_BODY()

    UPROPERTY()
    int32 P1 = INDEX_NONE;

    UPROPERTY()
    int32 P2 = INDEX_NONE;

    UPROPERTY()
    float RestLength = 0.0f;

    UPROPERTY()
    bool bBroken = false;

    // XPBD total lagrange multiplier (reset each substep)
    UPROPERTY()
    float Lambda = 0.0f;
};

UENUM(BlueprintType)
enum class EClothShapeType : uint8
{
    Rectangle UMETA(DisplayName = "Rectangle"),
    Ellipse UMETA(DisplayName = "Ellipse"),
    Diamond UMETA(DisplayName = "Diamond"),
    Triangle UMETA(DisplayName = "Triangle")
};

UENUM(BlueprintType)
enum class EPinRegionType : uint8
{
    Box UMETA(DisplayName = "Box"),
    Sphere UMETA(DisplayName = "Sphere")
};

UENUM(BlueprintType)
enum class EClothMaterialPreset : uint8
{
    Custom UMETA(DisplayName = "Custom"),
    Silk UMETA(DisplayName = "Silk (Light)"),
    Cotton UMETA(DisplayName = "Cotton (Shirt)"),
    Linen UMETA(DisplayName = "Linen"),
    Wool UMETA(DisplayName = "Wool"),
    Denim UMETA(DisplayName = "Denim"),
    Canvas UMETA(DisplayName = "Canvas (Heavy)")
};

UCLASS()
class PHYSICS_SANDBOX_API AClothSimActor : public AActor
{
    GENERATED_BODY()

public:
    AClothSimActor();

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
#if WITH_EDITOR
    virtual bool ShouldTickIfViewportsOnly() const override;
#endif

public:
    // ===== Cloth =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth")
    int32 ClothWidth = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth")
    int32 ClothHeight = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth")
    float Spacing = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth")
    FVector ClothOrigin = FVector(0.0f, 0.0f, 300.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth")
    bool bPinTopRow = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth")
    bool bHorizontalLayout = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Shape")
    EClothShapeType ClothShapeType = EClothShapeType::Rectangle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Shape", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    FVector2D ShapeSizeUV = FVector2D(1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    FVector2D ShapeCenterUV = FVector2D(0.5f, 0.5f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Shape")
    bool bInvertShapeSelection = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Source")
    bool bUseCustomStaticMesh = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Source", meta = (EditCondition = "bUseCustomStaticMesh"))
    UStaticMesh* SourceStaticMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Source", meta = (EditCondition = "bUseCustomStaticMesh"))
    FVector SourceMeshScale = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Source", meta = (EditCondition = "bUseCustomStaticMesh"))
    bool bCenterSourceMeshAtOrigin = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Source", meta = (EditCondition = "bUseCustomStaticMesh"))
    bool bUseSourceMeshUV0 = true;

    // ===== Pin Region =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin")
    bool bUsePinRegion = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUsePinRegion"))
    EPinRegionType PinRegionType = EPinRegionType::Box;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUsePinRegion", MakeEditWidget))
    FVector PinRegionCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUsePinRegion"))
    FVector PinRegionExtent = FVector(80.0f, 80.0f, 80.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUsePinRegion", ClampMin = "0.0"))
    float PinRegionRadius = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUsePinRegion"))
    bool bInvertPinRegion = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin")
    bool bUseExternalPinWeights = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUseExternalPinWeights", FilePathFilter = "csv"))
    FFilePath ExternalPinWeightFile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth|Pin", meta = (EditCondition = "bUseExternalPinWeights", ClampMin = "0.0", ClampMax = "1.0"))
    float SoftPinStiffness = 0.35f;

    // ===== Material Presets =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Material Preset")
    EClothMaterialPreset ClothMaterialPreset = EClothMaterialPreset::Custom;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Material Preset")
    bool bAutoApplyMaterialPreset = true;

    // ===== Simulation =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    float Gravity = 980.0f;

    // Total cloth mass in kg. Used to derive point masses for constraint solving.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Mass", meta = (ClampMin = "0.001"))
    float ClothTotalMassKg = 0.8f;

    // For custom static mesh cloth, distribute mass by triangle area (recommended).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Mass")
    bool bUseAreaWeightedPointMass = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Mass", meta = (ClampMin = "0.000001"))
    float MinPointMassKg = 0.0001f;

    // If true, WindForce is treated as force and converted via a = F / m.
    // If false, WindForce is treated as acceleration (legacy behavior).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Mass")
    bool bTreatWindAsForce = false;

    // Material weight factor for anti-stretch only.
    // Higher value means stiffer cloth (less elongation) without slowing gravity motion.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (ClampMin = "0.01", ClampMax = "4.0"))
    float ClothWeightScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Damping = 0.99f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    FVector WindForce = FVector::ZeroVector;

    // Legacy compatibility knob: keep it, use as multiplier on XPBD correction
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float Stiffness = 1.0f;

    // XPBD compliance: smaller = stiffer
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (ClampMin = "0.0"))
    float StretchCompliance = 0.000001f;

    // hard anti-explosion clamp, e.g. 1.05~1.15
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (ClampMin = "1.0"))
    float MaxStretchRatio = 1.08f;

    // Extra anti-stretch for hanging cloth (edges connected to pinned points)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HangingAntiStretch = 0.65f;

    // Near-inextensible mode: keeps cloth dynamics, but strongly limits edge stretching.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD")
    bool bUseNearInextensibleMode = false;

    // 1.0 means no stretch at all, 1.003 means up to 0.3% stretch.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (EditCondition = "bUseNearInextensibleMode", ClampMin = "1.0", ClampMax = "1.05"))
    float NearInextensibleStretchLimit = 1.003f;

    // How aggressively to push stretched edges back in each hard-constraint pass.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (EditCondition = "bUseNearInextensibleMode", ClampMin = "0.0", ClampMax = "1.0"))
    float NearInextensibleStrength = 1.0f;

    // Extra hard-constraint passes per solver iteration.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (EditCondition = "bUseNearInextensibleMode", ClampMin = "1", ClampMax = "8"))
    int32 NearInextensiblePasses = 2;

    // Additional recovery passes executed once per substep (independent from SolverIterations).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (EditCondition = "bUseNearInextensibleMode", ClampMin = "0", ClampMax = "32"))
    int32 PostStepLengthRecoveryPasses = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|XPBD", meta = (EditCondition = "bUseNearInextensibleMode", ClampMin = "0.0", ClampMax = "1.0"))
    float PostStepLengthRecoveryStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "64"))
    int32 SolverIterations = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.001", ClampMax = "0.05"))
    float FixedTimeStep = 0.016f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "8"))
    int32 MaxSubsteps = 4;

    // ===== Tear / Damage =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
    bool bEnableTearing = true;

    // if current length > RestLength + TearDistance, constraint breaks
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
    float TearDistance = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
    float DamageRadius = 18.0f;

    // ===== World Collision =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
    bool bEnableWorldCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
    TEnumAsByte<ECollisionChannel> WorldCollisionChannel = ECC_WorldStatic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
    float PointCollisionRadius = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
    float SurfaceOffset = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
    bool bUseWorldCollisionBroadphase = false;

    // how many world/local collision passes to run per substep when tearing is enabled
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (ClampMin = "1", ClampMax = "64"))
    int32 CollisionPassesWhenTearing = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (ClampMin = "0.0"))
    float MinWorldCollisionMoveDistance = 0.0f;

    // ===== Cloth-to-cloth collision =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth Collision")
    bool bEnableClothToClothCollision = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth Collision", meta = (ClampMin = "0.1"))
    float ClothToClothPointRadius = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth Collision", meta = (ClampMin = "0.0"))
    float ClothToClothSurfaceOffset = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth Collision", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ClothToClothFriction = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloth Collision")
    bool bPreventTearingFromClothToCloth = true;

    // ===== Optional local box collider =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Collider")
    bool bUseLocalBoxCollider = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Collider")
    FVector BoxCenter = FVector(0.0f, 0.0f, 80.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Collider")
    FVector BoxExtent = FVector(120.0f, 60.0f, 40.0f);

    // ===== Render =====
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Render")
    UProceduralMeshComponent* ClothMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    bool bRenderMesh = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    bool bRenderDoubleSided = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    bool bCreateMeshCollision = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    UMaterialInterface* ClothMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float UVTilingX = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float UVTilingY = 1.0f;

    // ===== Tear Visual =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear")
    bool bSmoothTearEdges = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bSmoothTearEdges", ClampMin = "0.0", ClampMax = "1.0"))
    float TearEdgeSmoothStrength = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bSmoothTearEdges", ClampMin = "1", ClampMax = "6"))
    int32 TearEdgeSmoothIterations = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear")
    bool bRenderTearStrands = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands", ClampMin = "1", ClampMax = "8"))
    int32 TearStrandCountPerEdge = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands", ClampMin = "0.0"))
    float TearStrandThickness = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands", ClampMin = "0.0"))
    float TearStrandSpread = 0.35f;

    // 0 = fully regular, 1 = highly random frayed look.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands", ClampMin = "0.0", ClampMax = "1.0"))
    float TearStrandIrregularity = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands", ClampMin = "1.0"))
    float TearStrandMaxStretchRatio = 2.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands", ClampMin = "1", ClampMax = "4096"))
    int32 MaxRenderedTearStrands = 512;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands"))
    bool bShowTearStrandsInGame = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands"))
    bool bShowTearStrandsInEditor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Tear", meta = (EditCondition = "bRenderTearStrands"))
    FColor TearStrandColor = FColor(220, 220, 220);

    // ===== Runtime =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime")
    bool bAutoRebuildOnRuntimeChange = true;

    // ===== Debug =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawPoints = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawConstraints = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawBrokenConstraints = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawWorldHits = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawBox = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    float PointDrawSize = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor ConstraintColor = FColor(200, 200, 200);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor BrokenConstraintColor = FColor::Red;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor PointColor = FColor::Yellow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor PinnedPointColor = FColor::Blue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor BoxColor = FColor::Cyan;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawPinRegionGizmo = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor PinRegionColor = FColor::Magenta;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bShowPinDebugInGame = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime")
    TArray<FClothPoint> Points;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime")
    TArray<FClothConstraint> Constraints;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Cloth")
    void RebuildCloth();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Simulation|Material Preset")
    void ApplySelectedMaterialPreset();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Cloth|Pin")
    void ReloadExternalPinWeights();

    UFUNCTION(BlueprintCallable, Category = "Damage")
    void ApplyDamageAtPoint(const FVector& WorldPoint, float Radius = -1.0f);

    UFUNCTION(BlueprintCallable, Category = "Damage")
    void ApplyDamageFromLineTrace(const FVector& TraceStart, const FVector& TraceEnd, float Radius = -1.0f);

private:
    float TimeAccumulator = 0.0f;
    mutable TArray<FHitResult> DebugWorldHits;

    // runtime cache for hot-rebuild
    int32 CachedClothWidth = 0;
    int32 CachedClothHeight = 0;
    float CachedSpacing = 0.0f;
    bool bCachedPinTopRow = false;
    bool bCachedHorizontalLayout = false;
    FVector CachedClothOrigin = FVector::ZeroVector;
    bool bCachedUseCustomStaticMesh = false;
    TObjectPtr<UStaticMesh> CachedSourceStaticMesh = nullptr;
    FVector CachedSourceMeshScale = FVector(1.0f, 1.0f, 1.0f);
    bool bCachedCenterSourceMeshAtOrigin = true;
    bool bCachedUseSourceMeshUV0 = true;
    EClothShapeType CachedClothShapeType = EClothShapeType::Rectangle;
    FVector2D CachedShapeSizeUV = FVector2D(1.0f, 1.0f);
    FVector2D CachedShapeCenterUV = FVector2D(0.5f, 0.5f);
    bool bCachedInvertShapeSelection = false;
    bool bCachedUsePinRegion = false;
    EPinRegionType CachedPinRegionType = EPinRegionType::Box;
    FVector CachedPinRegionCenter = FVector::ZeroVector;
    FVector CachedPinRegionExtent = FVector(80.0f, 80.0f, 80.0f);
    float CachedPinRegionRadius = 120.0f;
    bool bCachedInvertPinRegion = false;
    bool bCachedUseExternalPinWeights = false;
    float CachedSoftPinStiffness = 0.35f;
    FString CachedExternalPinWeightFilePath;
    EClothMaterialPreset CachedClothMaterialPreset = EClothMaterialPreset::Custom;
    bool bCachedAutoApplyMaterialPreset = true;
    float CachedClothTotalMassKg = 0.8f;
    bool bCachedUseAreaWeightedPointMass = true;
    float CachedMinPointMassKg = 0.0001f;

    // O(1) edge lookup
    TMap<uint64, int32> ConstraintLookup;

    // render cache
    TArray<FVector> RenderVertices;
    TArray<int32> RenderTriangles;
    TArray<int32> RenderShadingTriangles;
    TArray<int32> BaseTriangles;
    TArray<FVector> RenderNormals;
    TArray<FVector2D> RenderUV0;
    TArray<FVector2D> BaseUV0;
    TArray<FLinearColor> RenderVertexColors;
    TArray<FProcMeshTangent> RenderTangents;

    bool bTopologyDirty = true;
    bool bMeshSectionCreated = false;
    bool bRenderVerticesDirty = true;
    bool bUsingSourceMeshTopology = false;
    TArray<uint8> ClothCollisionContactMask;
    TArray<float> PointInverseMasses;
    TArray<float> ExternalPinWeights;
    TMap<FIntVector, float> ExternalPinWeightsByPosition;

private:
    int32 GetPointIndex(int32 X, int32 Y) const;
    uint64 MakeEdgeKey(int32 A, int32 B) const;

    void BuildPointsAndConstraints();
    void BuildGridPointsAndConstraints();
    bool BuildStaticMeshPointsAndConstraints();
    bool ShouldPointBeActive(int32 X, int32 Y, const FVector2D& UV) const;
    bool ShouldPinPoint(int32 X, int32 Y, const FVector& PositionLS, const FVector2D& UV) const;
    float GetExternalPinWeight(int32 PointIndex) const;
    bool TryGetExternalPinWeightByPosition(const FVector& Position, float& OutWeight) const;
    float GetExternalPinWeightForPosition(const FVector& Position, int32 PointIndex) const;
    float GetPointInverseMass(int32 PointIndex) const;
    bool LoadExternalPinWeights();
    void ApplyMaterialPreset(EClothMaterialPreset Preset);
    void UpdatePointMasses();
    void BuildConstraintLookup();

    void SimulateStep(float Dt);
    void Integrate(float Dt);
    void SolveConstraints(float Dt);
    void SolveCollisions();
    void SolveClothToClothCollisions();
    void SolveConstraintXPBD(FClothConstraint& Constraint, float Dt);
    void SolveWorldCollision(
        FClothPoint& Point,
        UWorld* World,
        const FTransform& ActorXform,
        const FCollisionQueryParams& Params,
        const FCollisionShape& SweepShape
    );
    void SolveLocalBoxCollision(FClothPoint& Point);
    void RePinPoints();
    void ResetConstraintLambdas();
    void EnforceMaxStretch();
    void EnforceLengthRecovery(int32 Passes, float Strength, float StretchLimit);
    void EnforceNearInextensibleStretch();
    void DebugDraw() const;
    bool HasPotentialWorldCollision(
        UWorld* World,
        const FTransform& ActorXform,
        const FCollisionQueryParams& Params
    ) const;
    bool BuildClothBoundsWorld(const FTransform& ActorXform, FBox& OutBounds) const;

    void UpdateRenderMesh();
    void ApplyTearEdgeSmoothing();
    void DrawTearStrands();
    void HandleRuntimeParameterChanges();
    void UpdateRuntimeCache();

    void InitializeRenderBuffers();
    void RebuildTriangleIndexBuffer();
    void RecalculateNormalsAndTangents();

    bool BreakConstraint(FClothConstraint& Constraint);

    bool IsConstraintIntact(int32 A, int32 B) const;
    bool IsTriangleValid(int32 A, int32 B, int32 C) const;
    bool AddTriangleIfValid(TArray<int32>& Triangles, int32 A, int32 B, int32 C) const;

    float DistancePointToSegment(const FVector& P, const FVector& A, const FVector& B) const;
    float DistanceSegmentToSegment(const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1) const;
};
