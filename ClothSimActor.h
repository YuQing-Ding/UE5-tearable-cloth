#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "ClothSimActor.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
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

    // ===== Simulation =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    float Gravity = 980.0f;

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime")
    TArray<FClothPoint> Points;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime")
    TArray<FClothConstraint> Constraints;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Cloth")
    void RebuildCloth();

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

    // O(1) edge lookup
    TMap<uint64, int32> ConstraintLookup;

    // render cache
    TArray<FVector> RenderVertices;
    TArray<int32> RenderTriangles;
    TArray<FVector> RenderNormals;
    TArray<FVector2D> RenderUV0;
    TArray<FLinearColor> RenderVertexColors;
    TArray<FProcMeshTangent> RenderTangents;

    bool bTopologyDirty = true;
    bool bMeshSectionCreated = false;
    bool bRenderVerticesDirty = true;

private:
    int32 GetPointIndex(int32 X, int32 Y) const;
    uint64 MakeEdgeKey(int32 A, int32 B) const;

    void BuildPointsAndConstraints();
    void BuildConstraintLookup();

    void SimulateStep(float Dt);
    void Integrate(float Dt);
    void SolveConstraints(float Dt);
    void SolveCollisions();
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
    void DebugDraw() const;
    bool HasPotentialWorldCollision(
        UWorld* World,
        const FTransform& ActorXform,
        const FCollisionQueryParams& Params
    ) const;

    void UpdateRenderMesh();
    void HandleRuntimeParameterChanges();
    void UpdateRuntimeCache();

    void InitializeRenderBuffers();
    void RebuildTriangleIndexBuffer();
    void RecalculateNormalsAndTangents();

    bool BreakConstraint(FClothConstraint& Constraint);

    bool IsConstraintIntact(int32 A, int32 B) const;
    bool IsTriangleValid(int32 A, int32 B, int32 C) const;
    void AddTriangleIfValid(TArray<int32>& Triangles, int32 A, int32 B, int32 C) const;

    float DistancePointToSegment(const FVector& P, const FVector& A, const FVector& B) const;
    float DistanceSegmentToSegment(const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1) const;
};
