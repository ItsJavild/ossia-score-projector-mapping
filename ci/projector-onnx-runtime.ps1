# The pinned 3.9.0 add-on's GitHub runtime archive is no longer published.
# Obtain the same 1.27.1 GPU runtime from Microsoft's NuGet feed. Package and
# hash match ossia/score-addon-onnx's upstream NuGet downloader.
$ErrorActionPreference = 'Stop'
$version = '1.27.1'
$package = 'microsoft.ml.onnxruntime.gpu.windows'
$expected = '07ad9d174f19ba47c13bf1989ee0c9a8d4832481e2eb51a16dd659a71162381f'
$archive = Join-Path $env:RUNNER_TEMP 'projector-onnx-runtime.zip'
$unpacked = Join-Path $env:RUNNER_TEMP 'projector-onnx-nuget'
$destination = 'C:/ossia-projector-onnxruntime'
Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$package/$version/$package.$version.nupkg" -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
  throw 'ONNX runtime package checksum mismatch'
}
Expand-Archive $archive -DestinationPath $unpacked
$headers = Join-Path $unpacked 'buildTransitive/native/include'
if (!(Test-Path "$headers/onnxruntime_c_api.h")) {
  $headers = Join-Path $unpacked 'build/native/include'
}
$native = Join-Path $unpacked 'runtimes/win-x64/native'
if (!(Test-Path "$headers/onnxruntime_c_api.h") -or !(Test-Path "$native/onnxruntime.dll") -or !(Test-Path "$native/onnxruntime.lib")) {
  throw 'ONNX package is missing its Windows x64 headers or runtime'
}
New-Item -ItemType Directory "$destination/include", "$destination/lib" -Force | Out-Null
Copy-Item "$headers/*" "$destination/include" -Recurse
Copy-Item "$native/*" "$destination/lib" -Recurse
Set-Content "$destination/VERSION_NUMBER" $version
Write-Output "Prepared verified ONNX Runtime $version from Microsoft NuGet"
