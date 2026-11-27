// foudre-banc : banc d'essai des plugins Foudre Audio.
// Charge un .vst3 sans Ableton, lui joue des notes ou un signal, enregistre un WAV, le mesure,
// le compare a une reference et verifie les regles de la gamme. Voir banc/LISEZMOI.md.

#include "Hote.h"
#include "FoudreRegles.h"
#include <cstdio>
#include <map>

void foudreConsoleUtf8();   // Console.cpp

using namespace foudre;
namespace regles = foudre::regles;

namespace
{
// ------------------------------------------------------------------ arguments : foudre-banc <commande> <fichiers...> --option valeur
struct Args
{
    juce::StringArray pos;
    std::multimap<juce::String, juce::String> opt;

    Args (int argc, char* argv[])
    {
        for (int i = 2; i < argc; ++i)
        {
            const juce::String a = juce::String::fromUTF8 (argv[i]);
            if (a.startsWith ("--"))
            {
                const auto cle = a.substring (2);
                const bool aValeur = i + 1 < argc && ! juce::String (argv[i + 1]).startsWith ("--");
                opt.emplace (cle, aValeur ? juce::String::fromUTF8 (argv[++i]) : juce::String ("1"));
            }
            else pos.add (a);
        }
    }
    bool a (const char* k) const { return opt.count (k) > 0; }
    juce::String v (const char* k, const juce::String& def = {}) const { auto it = opt.find (k); return it == opt.end() ? def : it->second; }
    double d (const char* k, double def) const { return a (k) ? v (k).getDoubleValue() : def; }
    juce::StringArray tous (const char* k) const { juce::StringArray r; auto pr = opt.equal_range (k); for (auto it = pr.first; it != pr.second; ++it) r.add (it->second); return r; }
};

juce::File fichier (const juce::String& chemin) { return juce::File::getCurrentWorkingDirectory().getChildFile (chemin); }
void dit (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); }
juce::String f1 (double x) { return x <= -239.0 ? juce::String ("-inf") : juce::String (x, 1); }
int echec (const juce::String& s) { std::fprintf (stderr, "ERREUR : %s\n", s.toRawUTF8()); return 2; }

juce::String barre (double db, double bas = -60.0)
{
    const int n = juce::jlimit (0, 30, (int) std::lround ((db - bas) / -bas * 30.0));
    return juce::String::repeatedString ("#", n);
}

// ------------------------------------------------------------------ affichage des mesures
void afficheMesures (const Audio& a, const juce::String& titre)
{
    const auto n = mesureNiveaux (a);
    const auto s = analyseSpectre (a);
    const auto clics = detecteClics (a);

    dit ("== " + titre + " (" + juce::String ((int) a.ch.size()) + " canaux, " + juce::String ((int) a.sr) + " Hz, " + juce::String (a.duree(), 2) + " s)");
    dit ("  Niveaux");
    dit ("    crete ............... " + f1 (n.creteDb) + " dBFS" + (n.creteDb > 0.0 ? "   !! depasse 0 dBFS" : ""));
    dit ("    RMS ................. " + f1 (n.rmsDb) + " dBFS");
    if (a.ch.size() > 1)
    {
        juce::String canaux ("    par canal ........... ");
        for (size_t c = 0; c < a.ch.size(); ++c)
        {
            Audio un; un.sr = a.sr; un.ch = { a.ch[c] };
            canaux << (c == 0 ? "G " : "   D ") << f1 (mesureNiveaux (un).rmsDb);
        }
        dit (canaux + " dBFS RMS");
    }
    dit ("    loudness integree ... " + f1 (n.lufsIntegre) + " LUFS   (max momentane " + f1 (n.lufsMomentaneMax) + ", max court terme " + f1 (n.lufsCourtTermeMax) + ")");
    dit ("    ecretage ............ " + juce::String (n.ecretes) + " echantillon(s) a 0 dBFS");
    dit ("    decalage DC ......... " + juce::String (n.dc, 5) + (std::fabs (n.dc) > 0.001 ? "   !! le signal n'est pas centre" : ""));
    dit ("    fin (100 ms) ........ " + f1 (n.finDb) + " dBFS" + (n.finDb > -60.0 ? "   (le son n'est pas eteint a la fin)" : ""));
    dit ("  Spectre (part de chaque octave)");
    for (size_t i = 0; i < s.centres.size(); ++i)
    {
        const double fc = s.centres[i];
        const juce::String nom = fc < 1000.0 ? juce::String ((int) std::lround (fc)) + " Hz" : juce::String (fc / 1000.0, fc < 10000.0 ? 1 : 0) + " kHz";
        dit ("    " + nom.paddedLeft (' ', 8) + "  " + f1 (s.bandesDb[i]).paddedLeft (' ', 6) + " dB  " + barre (s.bandesDb[i]));
    }
    dit ("    brillance (centroide) " + juce::String ((int) std::lround (s.centroide)) + " Hz ; aigus > 5 kHz " + f1 (s.aigusDb) + " dB ; graves < 120 Hz " + f1 (s.grosDb) + " dB");
    if (clics.empty()) dit ("  Clics suspects : aucun");
    else
    {
        dit ("  Clics suspects : " + juce::String ((int) clics.size()) + (clics.size() >= 20 ? " (ou plus)" : ""));
        for (auto& c : clics)
        {
            const juce::String quand = "    a " + juce::String (c.t, 3) + " s  ";
            const juce::String force ((int) std::lround (c.force));
            if (c.genre == Clic::saut)         dit (quand + "saut isole, " + force + " fois plus fort qu'autour");
            else if (c.genre == Clic::coupure) dit (quand + "coupure nette (-" + force + " dB en 5 ms) : relache trop court ?");
            else                               dit (quand + "attaque seche (+" + force + " dB en 5 ms) : normal si l'attaque est a 0");
        }
    }
    if (n.debut > 0.01) dit ("  !! le son commence par un saut (" + f1 (ampEnDb (n.debut)) + " dBFS des le premier echantillon)");
}

// ------------------------------------------------------------------ chargement + reglages (preset, etat, parametres)
bool prepare (Hote& h, const Args& args, const juce::File& vst3)
{
    h.sr = args.d ("sr", 48000.0);
    h.bloc = (int) args.d ("bloc", 512);
    h.tete.bpm = args.d ("tempo", 120.0);
    if (! h.charge (vst3)) { echec (h.erreur); return false; }

    auto& p = *h.plugin;
    if (args.a ("preset"))
    {
        const auto v = args.v ("preset");
        int idx = v.containsOnly ("0123456789") ? v.getIntValue() : -1;
        if (idx < 0) for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i).equalsIgnoreCase (v)) idx = i;
        if (idx < 0 || idx >= p.getNumPrograms()) { echec ("preset \"" + v + "\" introuvable (voir la commande infos)"); return false; }
        p.setCurrentProgram (idx);
        Hote::laisseTourner (300);
    }
    if (args.a ("programme"))   // Program Change MIDI (comme l'Hydrasynth) : pour les plugins qui ne publient pas leurs presets
    {
        if (! p.acceptsMidi()) { echec ("ce plugin ne recoit pas le MIDI : --programme impossible"); return false; }
        h.rendu ({ { 0.0, juce::MidiMessage::programChange (1, juce::jlimit (0, 127, args.v ("programme").getIntValue())) } }, 0.05);
        Hote::laisseTourner (500);
        h.rendu ({}, 0.05);
    }
    if (args.a ("etat"))
    {
        juce::MemoryBlock mb;
        if (! fichier (args.v ("etat")).loadFileAsData (mb)) { echec ("etat illisible : " + args.v ("etat")); return false; }
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        Hote::laisseTourner (300);
    }
    for (auto& kv : args.tous ("param"))     // --param "Nom=0.5" (valeur normalisee 0..1)  ou  --param "Nom=texte:-12 dB"
    {
        const auto nom = kv.upToFirstOccurrenceOf ("=", false, false).trim(), val = kv.fromFirstOccurrenceOf ("=", false, false).trim();
        juce::String msg;
        auto* q = h.trouveParam (nom, msg);
        if (q == nullptr) { echec (msg); return false; }
        const float x = val.startsWith ("texte:") ? q->getValueForText (val.substring (6)) : juce::jlimit (0.f, 1.f, val.getFloatValue());
        q->setValueNotifyingHost (x);
        dit ("  reglage : " + q->getName (200) + " = " + q->getCurrentValueAsText() + " (" + juce::String (q->getValue(), 3) + ")");
    }
    Hote::laisseTourner (200);
    return true;
}

std::vector<EvenementMidi> notes (const juce::StringArray& liste, int velocite, double duree)
{
    std::vector<EvenementMidi> e;
    for (auto& s : liste)
    {
        const int nt = juce::jlimit (0, 127, s.getIntValue());
        e.push_back ({ 0.0, juce::MidiMessage::noteOn (1, nt, (juce::uint8) velocite) });
        e.push_back ({ duree, juce::MidiMessage::noteOff (1, nt) });
    }
    return e;
}

// ------------------------------------------------------------------ commandes
int cmdInfos (const Args& args)
{
    if (args.pos.isEmpty()) return echec ("indiquez le .vst3");
    Hote h; if (! prepare (h, args, fichier (args.pos[0]))) return 2;
    auto& p = *h.plugin;
    const auto d = p.getPluginDescription();
    int visibles = 0; for (auto* q : p.getParameters()) if (! Hote::estCache (q)) ++visibles;
    dit ("== " + d.name + " (" + d.manufacturerName + ", version " + d.version + ")");
    dit ("  type ............ " + juce::String (h.estInstrument() ? "instrument" : "effet") + (p.acceptsMidi() ? ", recoit le MIDI" : ", sans MIDI"));
    dit ("  canaux .......... " + juce::String (p.getTotalNumInputChannels()) + " en entree, " + juce::String (p.getTotalNumOutputChannels()) + " en sortie");
    dit ("  latence ......... " + juce::String (p.getLatencySamples()) + " echantillons");
    dit ("  parametres ...... " + juce::String (visibles));
    dit ("  presets ......... " + juce::String (p.getNumPrograms()));
    for (int i = 0; i < p.getNumPrograms(); ++i) dit ("    " + juce::String (i).paddedLeft (' ', 3) + "  " + p.getProgramName (i));
    return 0;
}

int cmdParams (const Args& args)
{
    if (args.pos.isEmpty()) return echec ("indiquez le .vst3");
    Hote h; if (! prepare (h, args, fichier (args.pos[0]))) return 2;
    const auto filtre = args.pos.size() > 1 ? args.pos[1] : juce::String();
    auto& ps = h.plugin->getParameters();
    for (int i = 0; i < ps.size(); ++i)
    {
        auto* q = ps[i];
        if (Hote::estCache (q) || (filtre.isNotEmpty() && ! q->getName (200).containsIgnoreCase (filtre))) continue;
        dit ("  #" + juce::String (i).paddedLeft (' ', 4) + "  " + q->getName (200).paddedRight (' ', 28) + " = " + q->getCurrentValueAsText() + "  (" + juce::String (q->getValue(), 3) + ")");
    }
    return 0;
}

int cmdRendu (const Args& args)
{
    if (args.pos.isEmpty()) return echec ("indiquez le .vst3");
    const auto sortie = fichier (args.v ("sortie", "rendu.wav"));

    Audio entree; bool aEntree = false;
    if (args.a ("entree"))
    {
        juce::String err;
        if (! litAudio (fichier (args.v ("entree")), entree, err)) return echec (err);
        aEntree = true;
    }
    Args a2 = args;
    if (aEntree && ! args.a ("sr")) a2.opt.emplace ("sr", juce::String (entree.sr));   // le plugin tourne a la frequence du fichier d'entree

    Hote h; if (! prepare (h, a2, fichier (args.pos[0]))) return 2;

    std::vector<EvenementMidi> evs;
    const double queue = args.d ("queue", 1.5);
    double duree = args.d ("duree", 2.0);

    if (args.a ("midi"))
    {
        juce::MidiFile mf;
        juce::FileInputStream in (fichier (args.v ("midi")));
        if (! in.openedOk() || ! mf.readFrom (in)) return echec ("fichier MIDI illisible : " + args.v ("midi"));
        mf.convertTimestampTicksToSeconds();
        double fin = 0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (auto* e : *mf.getTrack (t))
                if (! e->message.isMetaEvent()) { evs.push_back ({ e->message.getTimeStamp(), e->message }); fin = juce::jmax (fin, e->message.getTimeStamp()); }
        duree = fin;
    }
    else if (h.estInstrument() || (h.plugin->acceptsMidi() && args.a ("notes")))
        evs = notes (juce::StringArray::fromTokens (args.v ("notes", "60"), ",", ""), (int) args.d ("velocite", 100), duree);

    if (! h.estInstrument() && ! aEntree)
    {
        juce::String err;
        entree = genereSignal (args.v ("signal", "sinus"), h.sr, duree, args.d ("niveau", -18.0), args.d ("freq", 440.0), err);
        if (err.isNotEmpty()) return echec (err);
        aEntree = true;
    }
    if (aEntree && ! args.a ("midi") && args.a ("entree")) duree = entree.duree();

    const auto out = h.rendu (evs, duree + queue, aEntree ? &entree : nullptr);
    juce::String err;
    if (! ecritWav (out, sortie, err)) return echec (err);
    if (args.a ("sauve-etat"))
    {
        juce::MemoryBlock mb; h.plugin->getStateInformation (mb);
        fichier (args.v ("sauve-etat")).replaceWithData (mb.getData(), mb.getSize());
    }
    if (aEntree && args.a ("garde-entree"))
        ecritWav (entree, sortie.getSiblingFile (sortie.getFileNameWithoutExtension() + "-entree.wav"), err);

    afficheMesures (out, sortie.getFullPathName());
    return 0;
}

int cmdMesure (const Args& args)
{
    if (args.pos.isEmpty()) return echec ("indiquez le fichier WAV");
    for (auto& f : args.pos)
    {
        Audio a; juce::String err;
        if (! litAudio (fichier (f), a, err)) return echec (err);
        afficheMesures (a, fichier (f).getFullPathName());
    }
    return 0;
}

int cmdCompare (const Args& args)
{
    if (args.pos.size() < 2) return echec ("indiquez deux fichiers : la reference (A) puis le rendu (B)");
    Audio a, b; juce::String err;
    if (! litAudio (fichier (args.pos[0]), a, err) || ! litAudio (fichier (args.pos[1]), b, err)) return echec (err);
    const auto na = mesureNiveaux (a), nb = mesureNiveaux (b);
    const auto sa = analyseSpectre (a), sb = analyseSpectre (b);

    auto ligne = [] (const juce::String& nom, double x, double y, const juce::String& u)
    { dit ("  " + nom.paddedRight (' ', 22) + f1 (x).paddedLeft (' ', 8) + f1 (y).paddedLeft (' ', 9) + ((y - x >= 0 ? "+" : "") + f1 (y - x)).paddedLeft (' ', 9) + " " + u); };

    dit ("== A = " + fichier (args.pos[0]).getFileName() + "   B = " + fichier (args.pos[1]).getFileName());
    dit ("  " + juce::String().paddedRight (' ', 22) + juce::String ("A").paddedLeft (' ', 8) + juce::String ("B").paddedLeft (' ', 9) + juce::String ("B - A").paddedLeft (' ', 9));
    ligne ("loudness integree", na.lufsIntegre, nb.lufsIntegre, "LUFS");
    ligne ("crete", na.creteDb, nb.creteDb, "dBFS");
    ligne ("aigus > 5 kHz", sa.aigusDb, sb.aigusDb, "dB");
    ligne ("graves < 120 Hz", sa.grosDb, sb.grosDb, "dB");
    dit ("  " + juce::String ("brillance (centroide)").paddedRight (' ', 22) + juce::String ((int) sa.centroide).paddedLeft (' ', 8) + juce::String ((int) sb.centroide).paddedLeft (' ', 9) + "          Hz");
    dit ("  Spectre (part de chaque octave, B - A)");
    for (size_t i = 0; i < juce::jmin (sa.centres.size(), sb.centres.size()); ++i)
    {
        const double fc = sa.centres[i], dlt = sb.bandesDb[i] - sa.bandesDb[i];
        const juce::String nom = fc < 1000.0 ? juce::String ((int) std::lround (fc)) + " Hz" : juce::String (fc / 1000.0, fc < 10000.0 ? 1 : 0) + " kHz";
        const int lg = juce::jlimit (0, 20, (int) std::lround (std::fabs (dlt) * 2.0));
        dit ("    " + nom.paddedLeft (' ', 8) + "  " + ((dlt >= 0 ? "+" : "") + f1 (dlt)).paddedLeft (' ', 6) + " dB  "
             + (dlt >= 0 ? juce::String::repeatedString (" ", 20) + "|" + juce::String::repeatedString ("+", lg)
                         : juce::String::repeatedString (" ", 20 - lg) + juce::String::repeatedString ("-", lg) + "|"));
    }
    dit ("  En clair :");
    const double dl = nb.lufsIntegre - na.lufsIntegre, rc = sa.centroide > 0 ? sb.centroide / sa.centroide : 1.0;
    dit (std::fabs (dl) < 1.0 ? "    meme volume (ecart < 1 dB)" : "    B est " + juce::String (dl > 0 ? "plus fort" : "moins fort") + " de " + f1 (std::fabs (dl)) + " dB");
    dit (rc > 1.15 ? "    B est plus brillant que A (centre du spectre " + juce::String ((rc - 1) * 100, 0) + " % plus haut)"
       : rc < 0.87 ? "    B est plus mat que A (centre du spectre " + juce::String ((1 - rc) * 100, 0) + " % plus bas)"
       :             "    meme brillance (a 15 % pres)");
    return 0;
}

// ------------------------------------------------------------------ verification des regles de la gamme
int cmdVerifie (const Args& args)
{
    if (args.pos.isEmpty()) return echec ("indiquez le .vst3");
    const auto vst3 = fichier (args.pos[0]);
    int ko = 0, alerte = 0;
    auto res = [&] (const char* etat, const juce::String& nom, const juce::String& detail)
    {
        const juce::String e (etat);
        if (e == "KO") ++ko; else if (e == "??") ++alerte;
        dit ("  [" + e + "] " + nom.paddedRight ('.', 34) + " " + detail);
    };
    auto nouveau = [&] (std::unique_ptr<Hote>& h) { h = std::make_unique<Hote>(); return prepare (*h, args, vst3); };

    std::unique_ptr<Hote> h;
    if (! nouveau (h)) return 2;
    const bool instrument = h->estInstrument();
    dit ("== Regles Foudre Audio : " + h->plugin->getName() + " (" + (instrument ? "instrument" : "effet") + ")");
    res ("OK", "Plugin VST3", "charge sans erreur");

    // 1. fenetre redimensionnable
    if (! h->plugin->hasEditor()) res ("KO", "Fenetre redimensionnable", "le plugin n'a pas d'interface");
    else
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (h->plugin->createEditorIfNeeded());
        if (ed == nullptr) res ("KO", "Fenetre redimensionnable", "l'interface ne s'ouvre pas");
        else res (ed->isResizable() ? "OK" : "KO", "Fenetre redimensionnable",
                  juce::String (ed->isResizable() ? "oui" : "non") + " (taille d'ouverture " + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) + ")");
        ed.reset();
        Hote::laisseTourner (100);
    }

    // 2. demarrage neutre
    if (instrument)
    {
        if (! nouveau (h)) return 2;
        const auto out = h->rendu (notes ({ "60" }, 100, 1.5), 3.0);
        const auto n = mesureNiveaux (out);
        const juce::String det = "note do 3 : crete " + f1 (n.creteDb) + " dBFS, max momentane " + f1 (n.lufsMomentaneMax) + " LUFS";
        if (n.creteDb > regles::instrumentCreteMaxDb) res ("KO", "Demarrage neutre", det + " -> sature");
        else if (n.lufsMomentaneMax > regles::instrumentLufsMaxAlert) res ("??", "Demarrage neutre", det + " -> fort pour un premier son");
        else if (n.creteDb < -60.0) res ("??", "Demarrage neutre", det + " -> presque muet : la note a-t-elle ete jouee ?");
        else res ("OK", "Demarrage neutre", det);
    }
    else
    {
        juce::String err;
        if (! nouveau (h)) return 2;
        const auto muet = h->rendu ({}, 2.0, nullptr);
        const auto nm = mesureNiveaux (muet);
        res (nm.creteDb > regles::effetBruitMaxDb ? "??" : "OK", "Demarrage neutre : silence", "entree muette -> sortie " + f1 (nm.creteDb) + " dBFS");

        if (! nouveau (h)) return 2;
        const auto sin = genereSignal ("sinus", h->sr, 3.0, -18.0, 1000.0, err);
        const auto out = h->rendu ({}, 3.0, &sin);
        Audio milieuE = sin, milieuS = out;                 // on mesure la partie stable (de 0,5 s a 2,5 s)
        const size_t d = (size_t) (0.5 * h->sr), lg = (size_t) (2.0 * h->sr);
        for (auto* x : { &milieuE, &milieuS }) for (auto& c : x->ch) c = std::vector<float> (c.begin() + (long) d, c.begin() + (long) (d + lg));
        const double ecart = mesureNiveaux (milieuS).rmsDb - mesureNiveaux (milieuE).rmsDb;
        const double dist = distorsionDb (milieuS, 1000.0);
        const juce::String detVol = "sinus 1 kHz -18 dBFS -> ecart " + ((ecart >= 0 ? "+" : "") + f1 (ecart)) + " dB";
        if (ecart > regles::effetEcartGainMaxDb)        res ("??", "Demarrage neutre : volume", detVol + " -> plus fort a l'ouverture");
        else if (ecart < -regles::effetEcartGainMaxDb)  res ("OK", "Demarrage neutre : volume", detVol + " -> plus bas (sans danger pour les oreilles)");
        else                                            res ("OK", "Demarrage neutre : volume", detVol);
        res (dist > regles::effetDistorsionMaxDb ? "??" : "OK", "Demarrage neutre : couleur", "harmoniques ajoutees " + f1 (dist) + " dB");
    }

    // 3. macros Hydrasynth (CC 16 a 23)
    if (! nouveau (h)) return 2;
    if (! h->plugin->acceptsMidi() && ! instrument) res ("--", "Macros Hydrasynth (CC 16-23)", "effet sans MIDI : normal, regle sans objet");
    else if (! h->plugin->acceptsMidi())            res ("KO", "Macros Hydrasynth (CC 16-23)", "instrument qui ne recoit pas le MIDI");
    else
    {
        auto& ps = h->plugin->getParameters();
        int bons = 0;
        juce::StringArray details;
        std::map<int, int> parParam;
        for (int cc = regles::premierCcMacro; cc <= regles::dernierCcMacro; ++cc)
        {
            const auto avant = h->valeurs();
            std::vector<EvenementMidi> evs;      // balayage 0 -> 127 : passe forcement par la position actuelle (mode reprise)
            for (int k = 0; k <= 8; ++k) evs.push_back ({ k * 0.02, juce::MidiMessage::controllerEvent (1, cc, juce::jmin (127, k * 16)) });
            h->rendu (evs, 0.25);
            Hote::laisseTourner (50);
            const auto apres = h->valeurs();
            juce::StringArray bouges;
            for (size_t i = 0; i < avant.size() && i < apres.size(); ++i)
                if (std::fabs (apres[i] - avant[i]) > 0.02f && ! Hote::estCache (ps[(int) i])) { bouges.add (ps[(int) i]->getName (60)); ++parParam[(int) i]; }
            if (! bouges.isEmpty()) ++bons;
            details.add ("CC" + juce::String (cc) + " (macro " + juce::String (regles::macroDuCc (cc)) + ") -> " + (bouges.isEmpty() ? juce::String ("rien ne bouge") : bouges.joinIntoString (", ")));
        }
        bool doublon = false; for (auto& kv : parParam) if (kv.second > 1) doublon = true;
        res (bons == regles::nombreMacros && ! doublon ? "OK" : "??", "Macros Hydrasynth (CC 16-23)",
             juce::String (bons) + " / " + juce::String (regles::nombreMacros) + " CC font bouger un reglage" + (doublon ? ", un reglage suit plusieurs CC" : ""));
        for (auto& s : details) dit ("         " + s);
    }

    // 4. ce que le banc ne peut pas verifier seul
    res ("--", "Cran magnetique au zero", "a verifier a la souris (le banc ne manipule pas l'interface)");

    dit ("  Resultat : " + juce::String (ko) + " KO, " + juce::String (alerte) + " a regarder");
    h.reset();
    return ko > 0 ? 1 : 0;
}

void aide()
{
    dit ("foudre-banc : banc d'essai des plugins Foudre Audio (sans Ableton)\n"
         "\n"
         "  foudre-banc infos    Plugin.vst3                 type, canaux, latence, liste des presets\n"
         "  foudre-banc params   Plugin.vst3 [filtre]        liste des reglages (nom, valeur)\n"
         "  foudre-banc rendu    Plugin.vst3 --sortie f.wav  joue et enregistre, puis mesure\n"
         "  foudre-banc mesure   f.wav [g.wav ...]           niveaux, loudness, spectre, clics\n"
         "  foudre-banc compare  reference.wav rendu.wav     ecarts de volume et de spectre\n"
         "  foudre-banc verifie  Plugin.vst3                 regles de la gamme (fenetre, demarrage neutre, CC 16-23)\n"
         "\n"
         "Options de rendu :\n"
         "  --notes 60,64,67   --velocite 100   --duree 2 (note tenue, s)   --queue 1.5 (apres relache, s)\n"
         "  --midi morceau.mid (remplace --notes)   --tempo 120\n"
         "  --preset 3 | --preset \"Nom\"   --programme 5 (Program Change MIDI)   --etat reglage.bin   --sauve-etat reglage.bin\n"
         "  --param \"Nom=0.5\" (0..1, repetable)   --param \"Nom=texte:-12 dB\"\n"
         "  effets : --entree son.wav | --signal sinus|bruit|balayage|impulsion|silence --niveau -18 --freq 440 [--garde-entree]\n"
         "  --sr 48000   --bloc 512");
}
} // namespace

int main (int argc, char* argv[])
{
    foudreConsoleUtf8();
    juce::ScopedJuceInitialiser_GUI initJuce;
    const juce::String cmd = argc > 1 ? juce::String (argv[1]) : juce::String();
    const Args args (argc, argv);

    int r = 0;
    if      (cmd == "infos")   r = cmdInfos (args);
    else if (cmd == "params")  r = cmdParams (args);
    else if (cmd == "rendu")   r = cmdRendu (args);
    else if (cmd == "mesure")  r = cmdMesure (args);
    else if (cmd == "compare") r = cmdCompare (args);
    else if (cmd == "verifie") r = cmdVerifie (args);
    else { aide(); r = cmd.isEmpty() || cmd == "aide" ? 0 : 2; }
    return r;
}
