// Capture d'ecran de l'interface d'un plugin (Windows) : sert a verifier une facade sans ouvrir Ableton.
// La fenetre du plugin est photographiee avec PrintWindow (rien ne s'affiche devant l'utilisateur plus d'une seconde).
#include <juce_graphics/juce_graphics.h>
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>

bool foudreCapture (void* fenetre, const juce::File& png, juce::String& erreur)
{
    HWND h = (HWND) fenetre;
    RECT r; GetClientRect (h, &r);
    const int w = r.right - r.left, ht = r.bottom - r.top;
    if (w <= 0 || ht <= 0) { erreur = "fenetre vide"; return false; }
    HDC ecranDc = GetDC (nullptr), dc = CreateCompatibleDC (ecranDc);
    BITMAPINFO bi {}; bi.bmiHeader.biSize = sizeof (bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -ht;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* pix = nullptr;
    HBITMAP bmp = CreateDIBSection (ecranDc, &bi, DIB_RGB_COLORS, &pix, nullptr, 0);
    HGDIOBJ old = SelectObject (dc, bmp);
    const BOOL ok = PrintWindow (h, dc, PW_CLIENTONLY | 0x00000002 /* PW_RENDERFULLCONTENT */);
    juce::Image img (juce::Image::RGB, w, ht, false);
    if (ok)
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        const auto* src = (const uint8_t*) pix;
        for (int y = 0; y < ht; ++y)
            for (int x = 0; x < w; ++x)
            {
                const uint8_t* s = src + ((size_t) y * (size_t) w + (size_t) x) * 4;
                bd.setPixelColour (x, y, juce::Colour (s[2], s[1], s[0]));
            }
    }
    SelectObject (dc, old); DeleteObject (bmp); DeleteDC (dc); ReleaseDC (nullptr, ecranDc);
    if (! ok) { erreur = "PrintWindow a echoue"; return false; }
    png.deleteFile();
    juce::FileOutputStream out (png);
    juce::PNGImageFormat fmt;
    if (! out.openedOk() || ! fmt.writeImageToStream (img, out)) { erreur = "ecriture impossible : " + png.getFullPathName(); return false; }
    return true;
}
#else
bool foudreCapture (void*, const juce::File&, juce::String& erreur) { erreur = "capture : Windows seulement"; return false; }
#endif
