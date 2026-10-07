# Quotes one argument for a Windows command line so that CommandLineToArgvW (and the C runtime)
# parse it back unchanged: arguments with a space, tab or quote are wrapped in quotes, backslashes
# before a quote are doubled and the quote escaped, trailing backslashes are doubled, an empty
# argument becomes "". Dot-sourced by tools/drive_game.ps1; tested by tests/test_idle_gate.py.
function ConvertTo-CommandLineArgument {
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Argument)
  if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') { return $Argument }
  $bsChar = [char]92
  $sb = New-Object System.Text.StringBuilder
  [void]$sb.Append('"')
  $bs = 0
  foreach ($c in $Argument.ToCharArray()) {
    if ($c -eq $bsChar) { $bs++; continue }
    if ($c -eq [char]34) {
      [void]$sb.Append($bsChar, $bs * 2 + 1)
      [void]$sb.Append('"')
    } else {
      if ($bs -gt 0) { [void]$sb.Append($bsChar, $bs) }
      [void]$sb.Append($c)
    }
    $bs = 0
  }
  if ($bs -gt 0) { [void]$sb.Append($bsChar, $bs * 2) }
  [void]$sb.Append('"')
  return $sb.ToString()
}
