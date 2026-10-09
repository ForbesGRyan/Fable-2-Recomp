# Pure helpers for tools/drive_game.ps1: its reading of -Plan, -Shots and -PlanShift. No game, no Win32;
# tests\test_idle_gate.py runs them as tables in PowerShell 7 and Windows PowerShell 5.1.
#
# The game parses FABLE2_AUTOPLAY itself (src/input/autoplay.h) and rejects the WHOLE plan on one bad token: it then
# runs with no input at all. So the driver accepts only what the game accepts, and nothing is dropped or passed through
# unread. Every function here throws with a one-line reason; the driver turns that into
# "REFUSED: bad plan: <reason>. Nothing launched." and exit code 2.

# A time in seconds as autoplay.h's ParseSeconds takes it: digits with at most one '.', and at least one digit. No
# sign, no exponent, no "NaN" or "Infinity". Stricter than the game in one point: at most 86400 (a day).
function ConvertTo-Fable2Seconds {
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Text, [Parameter(Mandatory)][string]$What)
  $s = $Text.Trim()
  if ($s -notmatch '^(?:[0-9]+\.?[0-9]*|\.[0-9]+)$') {
    throw "$What '$Text' is not a time in seconds (digits with at most one '.')"
  }
  $v = [double]::Parse($s, [Globalization.NumberStyles]::AllowDecimalPoint, [Globalization.CultureInfo]::InvariantCulture)
  if ($v -gt 86400) { throw "$What '$Text' is more than 86400 s" }
  return $v
}

# The time as it is handed to the game: plain decimal, at most three places.
function Format-Fable2Seconds {
  param([Parameter(Mandatory)][double]$Seconds)
  return $Seconds.ToString("0.###", [Globalization.CultureInfo]::InvariantCulture)
}

# -PlanShift: a whole number of seconds from 0 to 30.
function ConvertTo-Fable2PlanShift {
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Text)
  $s = $Text.Trim()
  if ($s -notmatch '^[0-9]{1,2}$' -or [int]$s -gt 30) { throw "-PlanShift '$Text' is not a whole number of seconds from 0 to 30" }
  return [int]$s
}

# -Shots: the screenshot times, each moved by $Shift.
function ConvertTo-Fable2ShotTimes {
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Shots, [double]$Shift = 0)
  $out = New-Object System.Collections.ArrayList
  foreach ($t in $Shots.Split(",")) {
    if (-not $t.Trim()) { continue }
    [void]$out.Add((ConvertTo-Fable2Seconds -Text $t -What "-Shots time") + $Shift)
  }
  return @($out)
}

# -Plan: "seconds:key" entries separated by commas (empty entries are skipped, as the game skips them). Every time is
# moved by $Shift. Returns Autoplay (the FABLE2_AUTOPLAY string: keys translated to pad buttons by $PadButtons; stick,
# view and capture entries as given), Count (its entries) and Events (-KeyboardInput: time and key name from $Keys).
# An entry is exactly seconds:key: a second ':' is refused, not dropped.
#   stick   LS=x/y/ms or RS=x/y/ms: x and y -100..100, ms 1..60000, each at most 6 digits (the game's ParseInt)
#   view    VIEW=off|split|overlay|native|pattern|shadow
#   capture DISC=frames/every: frames 1..100000, every 1..64, each at most 6 digits
function ConvertTo-Fable2Plan {
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Plan, [bool]$KeyboardInput = $false, [double]$Shift = 0,
        [Parameter(Mandatory)][hashtable]$PadButtons, [Parameter(Mandatory)][hashtable]$Keys)
  $steps = New-Object System.Collections.ArrayList
  $events = New-Object System.Collections.ArrayList
  foreach ($e in $Plan.Split(",")) {
    if (-not $e.Trim()) { continue }
    $parts = $e.Split(":")
    if ($parts.Count -ne 2) { throw "plan entry '$e' is not seconds:key (exactly one ':')" }
    $t = (ConvertTo-Fable2Seconds -Text $parts[0] -What "the time of plan entry '$e':") + $Shift
    $k = $parts[1].Trim()
    if ($KeyboardInput) {
      if (-not $Keys.ContainsKey($k.ToUpper())) { throw "unknown key '$k' in plan entry '$e' (known: $(($Keys.Keys | Sort-Object) -join ', '))" }
      [void]$events.Add([pscustomobject]@{ t = $t; k = $k.ToUpper() })
      continue
    }
    $ts = Format-Fable2Seconds -Seconds $t
    if ($k.Contains("=")) {
      # (-match ignores case, as the game's parser does)
      if ($k -match '^(?:LS|RS)=(-?[0-9]{1,6})/(-?[0-9]{1,6})/([0-9]{1,6})$') {
        $sx = [int]$Matches[1]; $sy = [int]$Matches[2]; $sms = [int]$Matches[3]
        if ($sx -lt -100 -or $sx -gt 100 -or $sy -lt -100 -or $sy -gt 100 -or $sms -lt 1 -or $sms -gt 60000) {
          throw "bad stick entry '$e': x and y must be -100..100 and ms 1..60000"
        }
      } elseif ($k -match '^VIEW=(?:off|split|overlay|native|pattern|shadow)$') {
      } elseif ($k -match '^DISC=([0-9]{1,6})/([0-9]{1,6})$') {
        $df = [int]$Matches[1]; $de = [int]$Matches[2]
        if ($df -lt 1 -or $df -gt 100000 -or $de -lt 1 -or $de -gt 64) {
          throw "bad capture entry '$e': frames must be 1..100000 and every 1..64"
        }
      } else {
        throw "plan entry '$e' is none of LS=x/y/ms, RS=x/y/ms, VIEW=off|split|overlay|native|pattern|shadow, DISC=frames/every"
      }
      [void]$steps.Add("${ts}:$k")
      continue
    }
    if (-not $PadButtons.ContainsKey($k.ToUpper())) {
      throw "key '$k' in plan entry '$e' has no pad button in autoplay mode (known: $(($PadButtons.Keys | Sort-Object) -join ', '))"
    }
    [void]$steps.Add("${ts}:$($PadButtons[$k.ToUpper()])")
  }
  return [pscustomobject]@{ Autoplay = ($steps -join ","); Count = $steps.Count; Events = @($events) }
}
