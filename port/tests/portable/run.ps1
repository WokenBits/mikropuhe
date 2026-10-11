# The 156 cases of tests/portable/run.sh, for a Windows without sh: same lines, same hashes.
param($Puhu, $Voices, $Cases)
$sha = [System.Security.Cryptography.SHA256]::Create()
function Q($a) {  # quoting as CommandLineToArgvW reads it
  $s = [regex]::Replace($a, '(\\*)"', '$1$1\"')
  $s = [regex]::Replace($s, '(\\+)$', '$1$1')
  '"' + $s + '"'
}
foreach ($line in [IO.File]::ReadAllLines($Cases, [Text.Encoding]::UTF8)) {
  $f = $line.Split("`t", 3)
  if ($f.Length -lt 3) { continue }
  if ($f[0] -eq 'T') { $a = "--voices-dir $(Q $Voices) --events -- $(Q $f[2])" } else { $a = "--voices-dir $(Q $Voices) --ui-script $(Q $f[2])" }
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = $Puhu; $psi.Arguments = $a; $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
  $p = [Diagnostics.Process]::Start($psi)
  $errTask = $p.StandardError.ReadToEndAsync()
  $ms = New-Object IO.MemoryStream
  $p.StandardOutput.BaseStream.CopyTo($ms)
  $p.WaitForExit()
  $bytes = $ms.ToArray()
  if ($errTask.Result) { $bytes += [Text.Encoding]::UTF8.GetBytes($errTask.Result) }
  $h = -join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
  "$($f[0]) $($f[1]) $h"
}
