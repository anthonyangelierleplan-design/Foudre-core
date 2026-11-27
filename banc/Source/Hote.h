#pragma once
// Mini-hote VST3 du banc d'essai : charge le .vst3, regle ses parametres, lui joue du MIDI ou un signal
// et enregistre ce qui sort, plus vite que le temps reel (rendu hors ligne).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Mesures.h"

namespace foudre
{
/** Position de lecture donnee au plugin : un DAW en lecture a tempo fixe (pour l'arpegiateur, les LFO synchronises...). */
struct TeteDeLecture final : juce::AudioPlayHead
{
    double bpm = 120.0, sr = 48000.0;
    juce::int64 pos = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        const double ppq = (double) pos / sr * bpm / 60.0;
        p.setBpm (bpm);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        p.setTimeInSamples (pos);
        p.setTimeInSeconds ((double) pos / sr);
        p.setPpqPosition (ppq);
        p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        p.setBarCount ((juce::int64) std::floor (ppq / 4.0));
        p.setIsPlaying (true);
        return p;
    }
};

struct EvenementMidi
{
    double t = 0;            // secondes
    juce::MidiMessage m;
};

class Hote
{
public:
    double sr = 48000.0;
    int bloc = 512;
    TeteDeLecture tete;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String erreur;

    ~Hote() { if (plugin != nullptr) plugin->releaseResources(); }

    static void laisseTourner (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    bool charge (const juce::File& f)
    {
        if (! f.exists()) { erreur = "fichier introuvable : " + f.getFullPathName(); return false; }
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> types;
        fmt.findAllTypesForFile (types, f.getFullPathName());
        if (types.isEmpty()) { erreur = "ce n'est pas un VST3 lisible : " + f.getFullPathName(); return false; }

        plugin = fmt.createInstanceFromDescription (*types[0], sr, bloc, erreur);
        if (plugin == nullptr) { if (erreur.isEmpty()) erreur = "le plugin refuse de se charger"; return false; }

        plugin->enableAllBuses();
        tete.sr = sr; tete.pos = 0;
        plugin->setPlayHead (&tete);
        plugin->setNonRealtime (true);
        plugin->prepareToPlay (sr, bloc);
        laisseTourner (300);
        return true;
    }

    bool estInstrument() const { return plugin->getPluginDescription().isInstrument || plugin->getTotalNumInputChannels() == 0; }

    // ---------------------------------------------------------------- parametres
    static bool estCache (juce::AudioProcessorParameter* q) { return q->getName (200).startsWith ("MIDI CC "); }   // parametres internes du VST3 (un par CC)

    juce::AudioProcessorParameter* trouveParam (const juce::String& nom, juce::String& msg) const
    {
        auto& ps = plugin->getParameters();
        if (nom.startsWithChar ('#'))
        {
            const int i = nom.substring (1).getIntValue();
            if (juce::isPositiveAndBelow (i, ps.size())) return ps[i];
            msg = "pas de parametre numero " + nom; return nullptr;
        }
        for (auto* q : ps) if (q->getName (200).equalsIgnoreCase (nom)) return q;
        juce::Array<juce::AudioProcessorParameter*> cand;
        for (auto* q : ps) if (! estCache (q) && q->getName (200).containsIgnoreCase (nom)) cand.add (q);
        if (cand.size() == 1) return cand[0];
        if (cand.isEmpty()) { msg = "aucun parametre ne s'appelle \"" + nom + "\" (voir la commande params)"; return nullptr; }
        msg = "plusieurs parametres contiennent \"" + nom + "\" :";
        for (int i = 0; i < juce::jmin (12, cand.size()); ++i) msg << " \"" << cand[i]->getName (200) << "\"";
        return nullptr;
    }

    std::vector<float> valeurs() const
    {
        std::vector<float> v;
        for (auto* q : plugin->getParameters()) v.push_back (q->getValue());
        return v;
    }

    // ---------------------------------------------------------------- rendu
    /** Joue les evenements MIDI (et le signal d'entree pour un effet) pendant 'duree' secondes ; renvoie la sortie.
        La latence annoncee par le plugin est retiree : la sortie est calee sur l'entree. */
    Audio rendu (std::vector<EvenementMidi> evs, double duree, const Audio* entree = nullptr)
    {
        auto& p = *plugin;
        const int nIn = p.getTotalNumInputChannels(), nOut = juce::jmax (1, p.getTotalNumOutputChannels());
        const int nCh = juce::jmax (nIn, nOut);
        const size_t lat = (size_t) juce::jmax (0, p.getLatencySamples());
        const size_t total = (size_t) std::llround (duree * sr) + lat;

        std::stable_sort (evs.begin(), evs.end(), [] (const EvenementMidi& a, const EvenementMidi& b) { return a.t < b.t; });

        Audio out; out.sr = sr;
        out.ch.assign ((size_t) nOut, std::vector<float> (total, 0.f));
        juce::AudioBuffer<float> buf (nCh, bloc);
        juce::MidiBuffer mb;
        size_t k = 0;

        for (size_t pos = 0; pos < total; pos += (size_t) bloc)
        {
            const int n = (int) std::min<size_t> ((size_t) bloc, total - pos);
            buf.setSize (nCh, n, false, false, true);
            buf.clear();
            mb.clear();

            if (entree != nullptr && ! entree->ch.empty())
                for (int c = 0; c < nIn; ++c)
                {
                    const auto& src = entree->ch[(size_t) juce::jmin (c, (int) entree->ch.size() - 1)];
                    for (int i = 0; i < n; ++i) { const size_t j = pos + (size_t) i; buf.setSample (c, i, j < src.size() ? src[j] : 0.f); }
                }

            for (; k < evs.size(); ++k)
            {
                const auto s = (juce::int64) std::llround (evs[k].t * sr);
                if (s >= (juce::int64) (pos + (size_t) n)) break;
                mb.addEvent (evs[k].m, (int) juce::jlimit<juce::int64> (0, n - 1, s - (juce::int64) pos));
            }

            tete.pos = (juce::int64) pos;
            p.processBlock (buf, mb);

            for (int c = 0; c < nOut && c < buf.getNumChannels(); ++c)
                std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, out.ch[(size_t) c].begin() + (long) pos);
        }
        if (lat > 0) for (auto& c : out.ch) c.erase (c.begin(), c.begin() + (long) lat);
        return out;
    }
};

// ------------------------------------------------------------------ fichiers et signaux
inline bool litAudio (const juce::File& f, Audio& a, juce::String& err)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { err = "impossible de lire " + f.getFullPathName(); return false; }
    const int n = (int) r->lengthInSamples, nc = (int) r->numChannels;
    juce::AudioBuffer<float> b (nc, n);
    r->read (&b, 0, n, 0, true, true);
    a.sr = r->sampleRate;
    a.ch.assign ((size_t) nc, {});
    for (int c = 0; c < nc; ++c) a.ch[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + n);
    return true;
}

inline bool ecritWav (const Audio& a, const juce::File& f, juce::String& err)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    auto os = std::make_unique<juce::FileOutputStream> (f);
    if (! os->openedOk()) { err = "impossible d'ecrire " + f.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), a.sr, (unsigned int) a.ch.size(), 32, {}, 0));   // 32 bits flottant : rien n'est ecrete
    if (w == nullptr) { err = "format WAV refuse"; return false; }
    os.release();
    std::vector<const float*> ptr;
    for (auto& c : a.ch) ptr.push_back (c.data());
    return w->writeFromFloatArrays (ptr.data(), (int) ptr.size(), (int) a.frames());
}

/** Signal de test pour un effet. Niveau en dBFS : crete pour sinus / balayage / impulsion, RMS pour le bruit. */
inline Audio genereSignal (const juce::String& type, double sr, double duree, double niveauDb, double freq, juce::String& err)
{
    Audio a; a.sr = sr;
    const size_t n = (size_t) std::llround (duree * sr);
    std::vector<float> x (n, 0.f);
    const double amp = std::pow (10.0, niveauDb / 20.0);
    juce::Random rnd (1234);

    if (type == "silence") {}
    else if (type == "sinus")
        for (size_t i = 0; i < n; ++i) x[i] = (float) (amp * std::sin (2.0 * pi * freq * (double) i / sr));
    else if (type == "balayage")         // glissando 20 Hz -> 20 kHz, vitesse constante en octaves
    {
        const double f1 = 20.0, f2 = juce::jmin (20000.0, sr * 0.45), L = duree / std::log (f2 / f1);
        for (size_t i = 0; i < n; ++i) { const double t = (double) i / sr; x[i] = (float) (amp * std::sin (2.0 * pi * f1 * L * (std::exp (t / L) - 1.0))); }
    }
    else if (type == "impulsion") { const size_t i = (size_t) std::llround (0.1 * sr); if (i < n) x[i] = (float) amp; }
    else if (type == "bruit")            // bruit rose (filtre de Paul Kellet), ramene au niveau RMS demande
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, s2 = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = rnd.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
            x[i] = (float) v; s2 += v * v;
        }
        const double g = n > 0 && s2 > 0 ? amp / std::sqrt (s2 / (double) n) : 0.0;
        for (auto& v : x) v = (float) (v * g);
    }
    else { err = "signal inconnu \"" + type + "\" (silence, sinus, bruit, balayage, impulsion)"; return a; }

    if (type != "impulsion")             // fondus de 5 ms : le signal de test ne cree pas de clic lui-meme
    {
        const size_t f = juce::jmin (n / 2, (size_t) std::llround (0.005 * sr));
        for (size_t i = 0; i < f; ++i) { const float g = (float) i / (float) f; x[i] *= g; x[n - 1 - i] *= g; }
    }
    a.ch = { x, x };
    return a;
}
} // namespace foudre
