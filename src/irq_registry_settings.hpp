#ifndef IRQ_REGISTRY_SETTINGS_HPP
#define IRQ_REGISTRY_SETTINGS_HPP

// User settings shared by the dispatcher, peripheral drivers and application.
// Change these values here, then rebuild all code using IRQ_Registry.

// Maximum registered handlers on one IRQ line. Increase for shared IRQs
// that need more than three handler objects/functions. Each additional slot
// uses IRQ_TABLE_SIZE * sizeof(void*) extra bytes in the dispatcher table.
#ifndef IRQ_MAX_SHARED
#define IRQ_MAX_SHARED 3
#endif

// Separate adapter pool for plain-function registrations across all IRQ lines.
#ifndef IRQ_MAX_FUNCTION_HANDLERS
#define IRQ_MAX_FUNCTION_HANDLERS 8
#endif

#endif // IRQ_REGISTRY_SETTINGS_HPP
