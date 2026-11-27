// Console Windows en UTF-8 (sinon les accents des noms de presets s'affichent mal).
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
void foudreConsoleUtf8() { SetConsoleOutputCP (CP_UTF8); }
#else
void foudreConsoleUtf8() {}
#endif
