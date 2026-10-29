# Journal Local Workspace Replacements on Shared Data

- Status: accepted
- Date: 2026-10-29
- Supersedes: None

## Context

Workspace records are stored together as raw text in a selected `<name>.dat` file on the shared Data FAT. Local row modification and deletion must preserve the untouched bytes, operate on files larger than the application's bounded buffers, and survive interruption without leaving a partially rewritten workspace. Main MTP may access the same Data tree concurrently, so local changes must detect a workspace version changed during user editing.

ADR-0011 establishes filesystem-mediated MTP access and staged promotion for host uploads. ADR-0012 establishes recoverable file-backed configuration updates, but workspace files are larger shared-data files and cannot be loaded as a complete snapshot in bounded target RAM.

## Decision

Local workspace row mutations will stream a complete replacement into a same-volume staging file, synchronize its contents, record transaction phases in alternating checksummed journal slots, and promote the replacement while retaining a backup for startup recovery. Recovery will run before workspace use and will restore the complete original before commit or retain the verified complete replacement after commit, provided the FAT volume remains mountable. The transaction will validate the source version immediately before promotion, including while MTP is active. The pinned MTP implementation has no shared application mutation gate, so this is optimistic conflict detection and does not exclude a host write racing after validation. Zephyr exposes file synchronization but no directory-sync operation; physical power-loss durability of FAT rename metadata remains a hardware qualification gate.

## Consequences

- Workspace changes do not truncate or rewrite the only copy in place and do not require memory proportional to file size.
- The persistence layer and fakes must support bounded staging, file synchronization, destination-absent same-volume rename, transaction state, cleanup, and recovery tests.
- Recovery artifacts share the MTP-visible Data filesystem and require reserved names, validation, and narrowly scoped cleanup.
- A version check detects changes made before validation but is not a substitute for a shared mutation lock; the remaining time-of-check/time-of-use window must be documented.
- Power-loss testing must validate the transaction record and filesystem behavior at every promotion phase on the target storage.
