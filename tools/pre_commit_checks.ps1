param()

$ErrorActionPreference = 'Stop'

$repoRoot = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repoRoot)) {
    exit 1
}
$repoRoot = $repoRoot.Trim()
$stashCommit = $null
$hookStatus = 0

& git -C $repoRoot diff --quiet --no-ext-diff --ignore-submodules --
if ($LASTEXITCODE -gt 1) { exit $LASTEXITCODE }
$hasUnstagedChanges = ($LASTEXITCODE -eq 1)
$untrackedFiles = & git -C $repoRoot ls-files --others --exclude-standard
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($hasUnstagedChanges -or $untrackedFiles) {
    # Quiet stash output is empty even when Git creates a stash; identify it by commit.
    $previousStash = & git -C $repoRoot rev-parse --verify --quiet refs/stash
    & git -C $repoRoot stash push --keep-index --include-untracked -q -m casedash-pre-commit-format
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'pre-commit: unable to stash unstaged changes'
        exit $LASTEXITCODE
    }
    $currentStash = & git -C $repoRoot rev-parse --verify --quiet refs/stash
    if ($currentStash -and $currentStash -ne $previousStash) {
        $stashCommit = $currentStash.Trim()
    }
}

try {
    # Check the whole staged snapshot so config and formatter updates also reformat existing files.
    Write-Host 'pre-commit: formatting staged snapshot'
    & cmd.exe /c "`"$repoRoot\format.cmd`" fix --restage"
    $hookStatus = $LASTEXITCODE
    if ($hookStatus -eq 0) {
        Write-Host 'pre-commit: running lint checks'
        & cmd.exe /c "`"$repoRoot\lint.cmd`""
        $hookStatus = $LASTEXITCODE
    }
} catch {
    Write-Host "pre-commit: $($_.Exception.Message)"
    $hookStatus = 1
} finally {
    if ($stashCommit) {
        try {
            # Restore only the unstaged delta. Reapplying the staged delta would conflict with formatting.
            $base = & git -C $repoRoot commit-tree "$stashCommit^2^{tree}" -m 'pre-commit staged snapshot'
            if ($LASTEXITCODE -ne 0) { throw 'unable to create recovery base' }
            $index = & git -C $repoRoot rev-parse "$stashCommit^2"
            if ($LASTEXITCODE -ne 0) { throw 'unable to read stashed index' }
            $parents = @('-p', $base.Trim(), '-p', $index.Trim())
            $untracked = & git -C $repoRoot rev-parse --verify --quiet "$stashCommit^3"
            if ($untracked) { $parents += @('-p', $untracked.Trim()) }
            $restore = & git -C $repoRoot commit-tree "$stashCommit^{tree}" @parents -m 'pre-commit unstaged snapshot'
            if ($LASTEXITCODE -ne 0) { throw 'unable to create recovery snapshot' }
            & git -C $repoRoot stash apply -q $restore.Trim()
            $restoreStatus = $LASTEXITCODE
        } catch {
            Write-Host "pre-commit: $($_.Exception.Message)"
            $restoreStatus = 1
        }
        if ($restoreStatus -ne 0) {
            Write-Host "pre-commit: unable to restore unstaged changes; preserved stash $stashCommit. Resolve it before committing."
            $hookStatus = 1
        } else {
            $currentStash = & git -C $repoRoot rev-parse --verify --quiet refs/stash
            if ($currentStash -eq $stashCommit) {
                & git -C $repoRoot stash drop -q 'stash@{0}'
                if ($LASTEXITCODE -ne 0) { $hookStatus = 1 }
            } else {
                Write-Host "pre-commit: restored unstaged changes; retained backup stash $stashCommit."
            }
        }
    }
}
exit $hookStatus
