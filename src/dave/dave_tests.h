#pragma once

// Self-checks for the MLS building blocks. Where a published test vector
// exists it is used; the rest are round-trip and structural checks, which
// catch internal inconsistency but cannot prove agreement with the spec - only
// a real peer can do that.
bool dave_self_test();
