macroScript PSB_ClothPinWeightExporter
category:"Physics_SandBox"
tooltip:"Cloth Pin Weight Exporter"
(
    global g_psb_pin_exporter_rollout

    fn psb_clamp01 v =
    (
        amax 0.0 (amin 1.0 v)
    )

    fn psb_getRedWeightFromMapVert c =
    (
        local w = c.x
        if w > 1.0 do w = w / 255.0
        psb_clamp01 w
    )

    fn psb_collectWeightsFromVertexColor node =
    (
        local msh = undefined
        try (msh = snapshotAsMesh node) catch (msh = undefined)
        if msh == undefined do return undefined

        local numVerts = msh.numverts
        local weights = for i = 1 to numVerts collect 0.0
        local counts = for i = 1 to numVerts collect 0

        if not (meshop.getMapSupport msh 0) then
        (
            free msh
            return undefined
        )

        local numFaces = msh.numfaces
        for f = 1 to numFaces do
        (
            local geoFace = getFace msh f
            local mapFace = undefined
            try (mapFace = meshop.getMapFace msh 0 f) catch (mapFace = undefined)

            if mapFace != undefined do
            (
                for c = 1 to 3 do
                (
                    local vIdx = geoFace[c]
                    local mvIdx = mapFace[c]

                    if (vIdx >= 1 and vIdx <= numVerts and mvIdx > 0) do
                    (
                        local mv = meshop.getMapVert msh 0 mvIdx
                        local w = psb_getRedWeightFromMapVert mv
                        weights[vIdx] += w
                        counts[vIdx] += 1
                    )
                )
            )
        )

        for i = 1 to numVerts do
        (
            if counts[i] > 0 then
                weights[i] = psb_clamp01 (weights[i] / counts[i])
            else
                weights[i] = 0.0
        )

        free msh
        weights
    )

    fn psb_exportWeightsCSV filePath weights scaleWeight invertWeight =
    (
        local stream = createFile filePath
        if stream == undefined do return false

        format "# Cloth pin weights exported from 3ds Max\n" to:stream
        format "# format: vertex_index,weight (vertex_index is zero-based)\n" to:stream

        local count = weights.count
        for i = 1 to count do
        (
            local w = weights[i]
            if invertWeight do w = 1.0 - w
            w = psb_clamp01 (w * scaleWeight)

            local ueIndex = i - 1
            format "%,%\n" ueIndex (formattedPrint w format:"0.6f") to:stream
        )

        close stream
        true
    )

    fn psb_sanitizeOutPath p =
    (
        if p == undefined then return undefined
        local outPath = trimRight (trimLeft p)
        local sepPos = findString outPath ";"
        if sepPos != undefined do
            outPath = trimRight (substring outPath 1 (sepPos - 1))

        if (getFilenameType outPath) == "" do
            outPath += ".csv"

        outPath
    )

    rollout g_psb_pin_exporter_rollout "Cloth Pin Weight Exporter"
    (
        local targetNode = undefined

        label lbl_source "Source: Vertex Color (Map 0, Red channel)" align:#left
        pickButton pb_pick "Pick Cloth Mesh" width:200 autoDisplay:true
        label lbl_target "Target: <none>" align:#left

        spinner sp_scale "Weight Scale:" range:[0.0,10.0,1.0] type:#float fieldwidth:60
        checkbox cb_invert "Invert Weights (1 - w)" checked:false

        button btn_export "Export CSV/TXT for Unreal" width:220 height:30
        button btn_help "Usage Tips" width:220

        on pb_pick picked obj do
        (
            if isValidNode obj then
            (
                targetNode = obj
                lbl_target.text = ("Target: " + obj.name)
            )
        )

        on btn_help pressed do
        (
            local helpText =
                ("1) Add VertexPaint modifier and paint RED as pin weight.\n" +
                 "2) Run this exporter and save .csv or .txt.\n" +
                 "3) In Unreal ClothSimActor, enable bUseExternalPinWeights and set ExternalPinWeightFile.\n" +
                 "4) Click ReloadExternalPinWeights.\n\n" +
                 "Important:\n" +
                 "- Keep topology and vertex order identical to mesh used in Unreal.\n" +
                 "- Exported index is zero-based.")
            messageBox helpText title:"Cloth Pin Weight Exporter"
        )

        on btn_export pressed do
        (
            if targetNode == undefined or not isValidNode targetNode then
            (
                messageBox "Please pick a cloth mesh node first." title:"Export Failed"
                return()
            )

            local weights = psb_collectWeightsFromVertexColor targetNode
            if weights == undefined or weights.count == 0 then
            (
                messageBox "No vertex color data found on Map Channel 0.\nPlease paint vertex color first (red channel)." title:"Export Failed"
                return()
            )

            local outPath = getSaveFileName caption:"Export Pin Weight File" types:"CSV (*.csv)|*.csv|Text (*.txt)|*.txt|"
            if outPath == undefined do return()
            outPath = psb_sanitizeOutPath outPath
            if outPath == undefined or outPath == "" do
            (
                messageBox "Invalid output path." title:"Export Failed"
                return()
            )

            local ok = psb_exportWeightsCSV outPath weights sp_scale.value cb_invert.checked
            if ok then
                messageBox ("Export completed:\n" + outPath) title:"Success"
            else
                messageBox "Failed to create output file." title:"Export Failed"
        )
    )

    try (destroyDialog g_psb_pin_exporter_rollout) catch ()
    createDialog g_psb_pin_exporter_rollout width:250
)

-- Auto-run when executed via "Scripting > Run Script..."
try
(
    macros.run "Physics_SandBox" "PSB_ClothPinWeightExporter"
)
catch
(
    messageBox ("Failed to launch Cloth Pin Weight Exporter.\nPlease open MAXScript Listener for details.") title:"Physics_SandBox"
)



