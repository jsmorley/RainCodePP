Run `Build\Build.ps1 -Target Test`. The tests compare token styles and folding with the fixtures and lex the same input incrementally at several chunk sizes. Check any fixture changes before using `-UpdateSnapshots` to refresh the expected output.

