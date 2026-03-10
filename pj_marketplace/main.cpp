#include <QtPlugin>

// Static Qt builds do not load platform plugins dynamically.
// They must be imported explicitly so the linker pulls in the
// plugin's static initializer. QT_STATIC is defined by Qt itself
// when built as a static library, so this block is a no-op with
// a shared Qt installation.
#ifdef QT_STATIC
#if defined(Q_OS_WIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined(Q_OS_LINUX)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#elif defined(Q_OS_MACOS)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif
#endif

int main()
{
  return 0;
}
