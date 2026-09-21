#pragma once

// TF2 module logging. Proof-of-life phase: verbose by default so the first injected
// sessions tell us everything. Trimmed to the anomaly-only contract once the module
// graduates past diagnostics.
namespace ns_tf2 {

void logInit();
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logShutdown();

} // namespace ns_tf2
