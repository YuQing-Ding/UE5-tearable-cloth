// Copyright YUQING DING. All Rights Reserved.

#include "ClothSimActor.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

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

void AClothSimActor::BeginPlay()
{
    Super::BeginPlay();
    RebuildCloth();
}

void AClothSimActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    HandleRuntimeParameterChanges();

    TimeAccumulator += DeltaTime;

    int32 Substeps = 0;
    while (TimeAccumulator >= FixedTimeStep && Substeps < MaxSubsteps)
    {
        SimulateStep(FixedTimeStep);
        TimeAccumulator -= FixedTimeStep;
        ++Substeps;
    }

    UpdateRenderMesh();
    DebugDraw();
}

void AClothSimActor::UpdateRuntimeCache()
{
    CachedClothWidth = ClothWidth;
    CachedClothHeight = ClothHeight;
    CachedSpacing = Spacing;
    bCachedPinTopRow = bPinTopRow;
    bCachedHorizontalLayout = bHorizontalLayout;
    CachedClothOrigin = ClothOrigin;
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
        !CachedClothOrigin.Equals(ClothOrigin, 0.01f);

    if (bNeedRebuild)
    {
        RebuildCloth();
    }
}

void AClothSimActor::RebuildCloth()
{
    Points.Reset();
    Constraints.Reset();
    ConstraintLookup.Reset();

    RenderVertices.Reset();
    RenderTriangles.Reset();
    RenderNormals.Reset();
    RenderUV0.Reset();
    RenderVertexColors.Reset();
    RenderTangents.Reset();

    DebugWorldHits.Reset();
    TimeAccumulator = 0.0f;

    bTopologyDirty = true;
    bMeshSectionCreated = false;

    ClothWidth = FMath::Max(1, ClothWidth);
    ClothHeight = FMath::Max(1, ClothHeight);
    Spacing = FMath::Max(1.0f, Spacing);
    SolverIterations = FMath::Max(1, SolverIterations);
    FixedTimeStep = FMath::Max(0.001f, FixedTimeStep);
    MaxSubsteps = FMath::Max(1, MaxSubsteps);
    Stiffness = FMath::Clamp(Stiffness, 0.01f, 1.0f);
    Damping = FMath::Clamp(Damping, 0.0f, 1.0f);
    StretchCompliance = FMath::Max(0.0f, StretchCompliance);
    MaxStretchRatio = FMath::Max(1.0f, MaxStretchRatio);
    PointCollisionRadius = FMath::Max(0.1f, PointCollisionRadius);
    TearDistance = FMath::Max(0.0f, TearDistance);
    DamageRadius = FMath::Max(1.0f, DamageRadius);
    UVTilingX = FMath::Max(0.001f, UVTilingX);
    UVTilingY = FMath::Max(0.001f, UVTilingY);

    BuildPointsAndConstraints();
    BuildConstraintLookup();
    InitializeRenderBuffers();
    RebuildTriangleIndexBuffer();
    UpdateRenderMesh();
    UpdateRuntimeCache();
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
    const float Diagonal = Spacing * FMath::Sqrt(2.0f);

    Points.Reserve((ClothWidth + 1) * (ClothHeight + 1));

    // 大概估算一下约束数量，减少扩容
    const int32 EstimatedConstraints =
        (ClothHeight + 1) * ClothWidth +          // horizontal
        (ClothWidth + 1) * ClothHeight +          // vertical
        (ClothWidth * ClothHeight * 2);           // diagonals

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

            P.PrevPosition = P.Position;
            P.Acceleration = FVector::ZeroVector;

            if (bPinTopRow && Y == 0)
            {
                P.bPinned = true;
                P.PinPosition = P.Position;
            }

            Points.Add(P);

            if (X > 0)
            {
                FClothConstraint C;
                C.P1 = GetPointIndex(X, Y);
                C.P2 = GetPointIndex(X - 1, Y);
                C.RestLength = Spacing;
                C.Lambda = 0.0f;
                Constraints.Add(C);
            }

            if (Y > 0)
            {
                FClothConstraint C;
                C.P1 = GetPointIndex(X, Y);
                C.P2 = GetPointIndex(X, Y - 1);
                C.RestLength = Spacing;
                C.Lambda = 0.0f;
                Constraints.Add(C);
            }

            if (X > 0 && Y > 0)
            {
                FClothConstraint C1;
                C1.P1 = GetPointIndex(X, Y);
                C1.P2 = GetPointIndex(X - 1, Y - 1);
                C1.RestLength = Diagonal;
                C1.Lambda = 0.0f;
                Constraints.Add(C1);

                FClothConstraint C2;
                C2.P1 = GetPointIndex(X - 1, Y);
                C2.P2 = GetPointIndex(X, Y - 1);
                C2.RestLength = Diagonal;
                C2.Lambda = 0.0f;
                Constraints.Add(C2);
            }
        }
    }
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

    for (int32 Y = 0; Y <= ClothHeight; ++Y)
    {
        for (int32 X = 0; X <= ClothWidth; ++X)
        {
            const int32 Idx = GetPointIndex(X, Y);

            RenderVertices[Idx] = Points[Idx].Position;
            RenderNormals[Idx] = FVector::UpVector;

            const float U = (static_cast<float>(X) / static_cast<float>(ClothWidth)) * UVTilingX;
            const float V = (static_cast<float>(Y) / static_cast<float>(ClothHeight)) * UVTilingY;
            RenderUV0[Idx] = FVector2D(U, V);

            RenderVertexColors[Idx] = FLinearColor::White;
            RenderTangents[Idx] = FProcMeshTangent(1.0f, 0.0f, 0.0f);
        }
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

    Integrate(Dt);
    ResetConstraintLambdas();

    for (int32 Iter = 0; Iter < SolverIterations; ++Iter)
    {
        SolveConstraints(Dt);
        EnforceMaxStretch();
        RePinPoints();
    }

    // final safety pass
    EnforceMaxStretch();
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

    for (FClothPoint& P : Points)
    {
        if (P.bPinned)
        {
            P.Position = P.PinPosition;
            P.PrevPosition = P.PinPosition;
            P.Acceleration = FVector::ZeroVector;
            continue;
        }

        P.Acceleration += GravityForce;
        P.Acceleration += WindForce;

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

    for (FClothPoint& P : Points)
    {
        if (bEnableWorldCollision)
        {
            SolveWorldCollision(P);
        }

        if (bUseLocalBoxCollider)
        {
            SolveLocalBoxCollision(P);
        }
    }
}

void AClothSimActor::SolveConstraintXPBD(FClothConstraint& Constraint, float Dt)
{
    if (Constraint.bBroken)
    {
        return;
    }

    if (!Points.IsValidIndex(Constraint.P1) || !Points.IsValidIndex(Constraint.P2))
    {
        return;
    }

    FClothPoint& P1 = Points[Constraint.P1];
    FClothPoint& P2 = Points[Constraint.P2];

    const FVector Delta = P1.Position - P2.Position;
    const float Distance = Delta.Length();

    if (Distance <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    if (bEnableTearing && TearDistance > 0.0f && Distance > Constraint.RestLength + TearDistance)
    {
        BreakConstraint(Constraint);
        return;
    }

    const float C = Distance - Constraint.RestLength;
    const FVector N = Delta / Distance;

    const float W1 = P1.bPinned ? 0.0f : 1.0f;
    const float W2 = P2.bPinned ? 0.0f : 1.0f;
    const float WSum = W1 + W2;

    if (WSum <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float Alpha = StretchCompliance / (Dt * Dt);
    const float Denominator = WSum + Alpha;

    if (Denominator <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    float DeltaLambda = (-C - Alpha * Constraint.Lambda) / Denominator;
    DeltaLambda *= Stiffness;

    Constraint.Lambda += DeltaLambda;

    const FVector Correction = DeltaLambda * N;

    if (!P1.bPinned)
    {
        P1.Position += W1 * Correction;
    }

    if (!P2.bPinned)
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

        FClothPoint& P1 = Points[C.P1];
        FClothPoint& P2 = Points[C.P2];

        const FVector Delta = P1.Position - P2.Position;
        const float Distance = Delta.Length();

        if (Distance <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const float MaxAllowed = C.RestLength * MaxStretchRatio;

        if (Distance <= MaxAllowed)
        {
            continue;
        }

        if (bEnableTearing && TearDistance > 0.0f && Distance > C.RestLength + TearDistance)
        {
            BreakConstraint(C);
            continue;
        }

        const FVector Dir = Delta / Distance;
        const float Excess = Distance - MaxAllowed;

        if (!P1.bPinned && !P2.bPinned)
        {
            const FVector Half = Dir * (Excess * 0.5f);
            P1.Position -= Half;
            P2.Position += Half;
        }
        else if (P1.bPinned && !P2.bPinned)
        {
            P2.Position += Dir * Excess;
        }
        else if (!P1.bPinned && P2.bPinned)
        {
            P1.Position -= Dir * Excess;
        }
    }
}

void AClothSimActor::SolveWorldCollision(FClothPoint& Point)
{
    if (Point.bPinned)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const FTransform ActorXform = GetActorTransform();

    const FVector StartWS = ActorXform.TransformPosition(Point.PrevPosition);
    const FVector EndWS = ActorXform.TransformPosition(Point.Position);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(ClothPointSweep), false, this);
    Params.bReturnPhysicalMaterial = false;

    FHitResult Hit;
    const FQuat Rotation = FQuat::Identity;
    const FCollisionShape Shape = FCollisionShape::MakeSphere(PointCollisionRadius);

    const bool bHit = World->SweepSingleByChannel(
        Hit,
        StartWS,
        EndWS,
        Rotation,
        WorldCollisionChannel,
        Shape,
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

void AClothSimActor::SolveLocalBoxCollision(FClothPoint& Point)
{
    if (Point.bPinned)
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
        if (P.bPinned)
        {
            P.Position = P.PinPosition;
            P.PrevPosition = P.PinPosition;
            P.Acceleration = FVector::ZeroVector;
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

    return IsConstraintIntact(A, B) &&
        IsConstraintIntact(B, C) &&
        IsConstraintIntact(C, A);
}

void AClothSimActor::AddTriangleIfValid(TArray<int32>& Triangles, int32 A, int32 B, int32 C) const
{
    if (IsTriangleValid(A, B, C))
    {
        Triangles.Add(A);
        Triangles.Add(B);
        Triangles.Add(C);
    }
}

void AClothSimActor::RebuildTriangleIndexBuffer()
{
    RenderTriangles.Reset();

    const int32 EstimatedTriCount = ClothWidth * ClothHeight * (bRenderDoubleSided ? 12 : 6);
    RenderTriangles.Reserve(EstimatedTriCount);

    for (int32 Y = 0; Y < ClothHeight; ++Y)
    {
        for (int32 X = 0; X < ClothWidth; ++X)
        {
            const int32 I00 = GetPointIndex(X, Y);
            const int32 I10 = GetPointIndex(X + 1, Y);
            const int32 I01 = GetPointIndex(X, Y + 1);
            const int32 I11 = GetPointIndex(X + 1, Y + 1);

            AddTriangleIfValid(RenderTriangles, I00, I10, I11);
            AddTriangleIfValid(RenderTriangles, I00, I11, I01);

            if (bRenderDoubleSided)
            {
                AddTriangleIfValid(RenderTriangles, I11, I10, I00);
                AddTriangleIfValid(RenderTriangles, I01, I11, I00);
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

    for (int32 i = 0; i < VertexCount; ++i)
    {
        RenderNormals[i] = FVector::ZeroVector;
    }

    for (int32 T = 0; T < RenderTriangles.Num(); T += 3)
    {
        const int32 IA = RenderTriangles[T];
        const int32 IB = RenderTriangles[T + 1];
        const int32 IC = RenderTriangles[T + 2];

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

    for (int32 Y = 0; Y <= ClothHeight; ++Y)
    {
        for (int32 X = 0; X <= ClothWidth; ++X)
        {
            const int32 Idx = GetPointIndex(X, Y);

            FVector TangentDir = FVector::ForwardVector;

            if (X < ClothWidth)
            {
                const int32 RightIdx = GetPointIndex(X + 1, Y);
                TangentDir = (RenderVertices[RightIdx] - RenderVertices[Idx]).GetSafeNormal();
            }
            else if (X > 0)
            {
                const int32 LeftIdx = GetPointIndex(X - 1, Y);
                TangentDir = (RenderVertices[Idx] - RenderVertices[LeftIdx]).GetSafeNormal();
            }

            if (TangentDir.IsNearlyZero())
            {
                TangentDir = FVector::ForwardVector;
            }

            RenderTangents[Idx] = FProcMeshTangent(TangentDir, false);
        }
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
    }

    for (int32 i = 0; i < VertexCount; ++i)
    {
        RenderVertices[i] = Points[i].Position;
    }

    const bool bHadTopologyDirty = bTopologyDirty;

    if (bTopologyDirty)
    {
        RebuildTriangleIndexBuffer();
    }

    RecalculateNormalsAndTangents();

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
    else
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

    if (ClothMaterial)
    {
        ClothMesh->SetMaterial(0, ClothMaterial);
    }

    ClothMesh->SetVisibility(true);
    ClothMesh->SetCollisionEnabled(
        bCreateMeshCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision
    );
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

    if (bDrawConstraints || bDrawBrokenConstraints)
    {
        for (const FClothConstraint& C : Constraints)
        {
            if (!Points.IsValidIndex(C.P1) || !Points.IsValidIndex(C.P2))
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

    if (bDrawWorldHits)
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