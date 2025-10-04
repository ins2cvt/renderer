$projectPath = $args[0]
$outputPath = $args[1]

$slangcPath = Join-Path -Path $env:VULKAN_SDK -ChildPath "bin/slangc.exe"

# TODO: Do not hardcode the entrypoints into this file.

$shaderSourcePath = Join-Path -Path $projectPath -ChildPath "src/shader.slang"
$shaderOutputPath = Join-Path -Path $outputPath -ChildPath "shader.spv"

Invoke-Expression "$slangcPath $shaderSourcePath -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexShader -entry fragmentShader -o $shaderOutputPath"

$resourceDirectoryPath = Join-Path -Path $projectPath -ChildPath "res"

Copy-Item -Path $resourceDirectoryPath -Destination $outputPath -Recurse -Force
