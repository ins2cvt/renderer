$sourcePath = $args[0]
$outputPath = $args[1]

$slangcPath = Join-Path -Path $env:VULKAN_SDK -ChildPath "bin/slangc.exe"

# TODO: Do not hardcode the entrypoints into this file.
$compileShadersCommand = "$slangcPath $($sourcePath) -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexShader -entry fragmentShader -o $($outputPath)"

Invoke-Expression $compileShadersCommand
