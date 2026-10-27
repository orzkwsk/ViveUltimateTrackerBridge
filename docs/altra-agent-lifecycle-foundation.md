# Altra A-B1 Native Agent lifecycle foundation

Scope: the Windows console `vut_service` owns one `AgentLifecycle`, whose
runtime aggregate owns the existing `ServiceRuntime`, loopback sender and
revision journal. A-B1 does not provide tray/WPF, GUI IPC, a production deployment,
or hardware ownership qualification. Windows 10 or later is required for atomic
process creation into a Job Object. No legacy control-host/UI source is imported.

## Instance identity and entry

The key is the length-delimited tuple of canonical absolute config path,
canonical absolute revision-journal path, and output port. Existing path prefixes
are resolved through Windows file handles, including reparse/short-path aliases;
a missing journal uses its resolved existing ancestor plus normalized suffix.
Windows invariant case folding and SHA-256 produce a bounded, versioned
`Global\Monaka.Altra.Agent.v1.<digest>` mutex name. Diagnostics, source/RF/slot,
PID and executable names are not key fields. A diagnostics toggle cannot create
a second owner. File contents are not persistent ownership identity.

Mutex creation requests initial ownership atomically. Existing objects, access
failure and path/hash failure reject startup before the runtime factory can open
the journal or construct/start Backend. An existing abandoned object is also
rejected while another handle retains it. This favors fail-closed behavior over
unproven recovery. Global namespace coordinates sessions; access denial does not
fall back to a per-session key. No process discovery or termination is used to
claim the lock. This is not a security boundary against malicious same-user
path/object tampering. Different config/journal/port tuples have different
software namespaces and do **not** prove simultaneous dongle access is safe.

CLI remains `vut_service CONFIG REVISION_JOURNAL [PORT] [--diagnostics]`.
`--help` and `--check-session CONFIG JOURNAL` retain their former behavior.
The latter bypasses the Agent mutex but retains the journal's exclusive open,
append + FlushFileBuffers, torn/corrupt rejection and overflow failure.
The journal implementation is moved unchanged; its lock is not replaced by the
instance mutex. Normal startup obtains the mutex before opening the journal.

## Owner thread and shutdown

Construction, startup, polling, status/child access and destruction belong to
the same tracking thread. Recursive lifecycle work and foreign-thread calls
are rejected. `requestStop()` is the only cross-thread operation: it sets an
atomic flag, with no Backend/Runtime call. A later tray message-loop thread may
use this stop seam and a separately reviewed command queue, never invoke Runtime
directly or mix blocking HID polling into a window callback.

States are Starting, Running, Stopping, Stopped and Faulted. A lifecycle cannot
restart. Ctrl-C/Ctrl-Break and SIGINT/SIGTERM signal the console loop; the owner
thread requests shutdown. The console handler does not access a Runtime pointer.
Runtime/transport failure produces Faulted and exits; there is no reconnect loop.

Shutdown ordering is explicit:

1. Reject future work/child launches and enter Stopping.
2. Invoke the runtime aggregate's shutdown once. Existing Runtime clears Backend
   callbacks, closes HID and handles its bounded publication/diagnostic state.
3. Destroy Runtime before its sender and journal dependencies. Diagnostics retain
   the existing bounded Runtime event buffers; no retry/drain loop is introduced.
4. Close the non-inheritable child Job handle; wait up to five seconds for the
   exact child process handle to signal. An unconfirmed exit faults and retains
   the instance lock. Destruction retries confirmation, then terminates the Agent
   rather than reporting clean release if it still cannot prove child exit.
5. Release the mutex and exit. Repeated shutdown is idempotent and destructors
   do not throw. Foreign-thread destruction terminates rather than violating
   Runtime/mutex thread ownership.

## One owned child, no adoption

The API accepts an executable and argument vector, quotes arguments without a
shell and holds the `CreateProcessW` process handle. There is no PID adoption API.
The unnamed, non-inheritable Job has kill-on-close and active-process limit one.
The latter also rejects child-spawned helpers: supporting more than Agent + one
child would require a separately reviewed scope change.

`PROC_THREAD_ATTRIBUTE_JOB_LIST` assigns the child during creation; the initial
thread remains suspended until membership is confirmed. Assignment failure
fails creation before execution. Resume/membership failure terminates only the
exact newly created process handle. The Agent retains that handle immediately
after creation, before any admission check. Only a signaled exact handle confirms
exit; a successful termination request alone does not. If termination or the wait
cannot confirm exit, the Agent faults, rejects further work, shuts down tracking
once and closes the Job while retaining the child handle and instance mutex until
an exact exit wait succeeds. Repeated shutdown can retry confirmation without
restarting Runtime or turning Faulted into Stopped. If the initial cleanup wait
already confirms exit, the rejected child is cleared and tracking may continue.
Windows killing the owner closes its last
Job handle and removes the child. A child exit/crash is observed as Exited and
does not stop tracking; a later explicit launch can replace that exited child.
No WPF executable is launched in A-B1. Graceful GUI close/IPC is deferred; current
Agent teardown enforces owned-child termination via its Job.

The atomic Job assignment avoids the orphan window in create-suspended followed
by separate assignment, documented by [Microsoft](https://devblogs.microsoft.com/oldnewthing/20230209-00/?p=107812/).
See also [UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
and [CreateMutexW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw).

## Software gates and negative controls

Windows CTest adds `agent_lifecycle` and `agent_process_ownership`; both are
required in the release manifest without removing the previous 24 groups.
The additional `agent_admission_confirmation` gate injects resume/membership,
termination and wait failures at the Win32 boundary around production source.
It checks retained child/mutex ownership, denied re-spawn, retry confirmation,
exact-child cleanup, one Runtime shutdown and lock reacquisition after confirmation.
The test launches only its own suspended executable and includes an independent
exact handle for cleanup even when the regression is deliberately reintroduced.
The fixture links only lifecycle + bcrypt, never Backend/HID. Its runtime counts
factory/start/shutdown/destruction and journal mutation. Compile-time
`VUT_AGENT_TESTING` enables an assignment-failure injection only in this test
executable; the release payload contains no fixture or production fake CLI.

P1 rejects duplicate case/dot-path identity, including diagnostics differences,
before fake HID/factory/journal activity. P2 checks graceful stop/child cleanup.
P3 terminates the harness-owned Agent and checks child residue. P4 observes child
exit with continuing runtime polls. P5 injects invalid Job assignment and proves
no child is admitted or executes. P6 preserves a same-executable external fixture.
P7 reacquires after shutdown. P8 isolates different config identity in the software
lock; actual journal/HID sharing is not authorized by that result. Unit coverage
also checks port/journal differences, creation of a previously absent journal,
wrong-thread calls, stop requests, faults and idempotent shutdown.

`tests/agent_negative_controls.py --work NEW_DIRECTORY --cmake CMAKE_EXE` builds
five temporary mutants and requires a nonzero harness result at the expected
detector: missing config key, removed Job kill-on-close, duplicate runtime start,
external-process kill, and double Runtime shutdown. External-kill mutation is
limited to a harness-created fixture whose PID and creation time match; it never
discovers or targets an existing user's process. Mutants and their logs remain
outside the product source. Release checks use explicit exceptions/CHECK, not
assertions removable by NDEBUG or Python -O.

## Boundaries retained

ServiceRuntime/Backend, Observation adapter/publisher/session, wire schema and
pin, explicit setup/pairing, and tracking algorithms are unchanged. The source
closure gate follows the relocated service implementation while retaining the
single-start/no-reconnect checks and adding pre-factory ownership/Job requirements.

NOT RUN: real dongle exclusive ownership, Hub contention, HID release timing,
pairing during shutdown, real tracker reconnect, multi-tracker, USB-handle
residue and physical RF behavior. Native process tests establish software
ownership only. HistoryFull policy, identity provider, durable control/IPC,
final Native Agent/tray/WPF and device contention qualification remain open.
G2-OWNER remains BLOCKED; Phase B/Issue16 removal is not authorized. A-B2 does
not start automatically after this Draft PR.
