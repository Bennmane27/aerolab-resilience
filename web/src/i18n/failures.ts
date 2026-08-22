// AEROLAB RESILIENCE - failure catalog entries, French and English (UI-025).
//
// These mirror docs/failures/known_failures.md. Kept in their own module because
// they are the longest prose in the interface and because a reader comparing the
// page against the repository file must see the same nine entries in the same
// order.
import type { Lang } from "./strings";

export interface FailureEntry {
  id: string;
  title: string;
  what: string;
  why: string;
  status: string;
  reproduce: string;
}

const en: FailureEntry[] = [
  {
    id: "KF-003",
    title: "A discontinuous bank angle in the ground truth destroyed the filter",
    what:
      "On the turning approach profile, every architecture with an integrity layer diverged to kilometres of error on a fault-free run, and isolated three sources while doing it. The plain filter, with no integrity layer, was fine at 23 m.",
    why:
      "The horizontal track was a chain of constant-curvature segments, so curvature stepped at the arc entry. A coordinated-turn bank angle is a function of curvature, so the bank stepped too — from level to 20 degrees in zero time. That is an infinite roll rate. No gyroscope can report it, so the filter had no way to know the aircraft had rolled, inherited a permanent attitude error, and everything downstream followed. Nothing was wrong with the estimator; the ground truth was not physically realisable.",
    status:
      "Fixed. The bank angle is now derived from a curvature blended over a roll-in distance, which is what an aircraft actually does. A regression test bounds the body rates over the whole profile.",
    reproduce: "aerolab_tests --gtest_filter=GroundTruth.BodyRatesStayBoundedThroughTheTurn",
  },
  {
    id: "KF-007",
    title: "The detectability sweep reported detection that had not happened",
    what:
      "The first run of the sweep reported 100 % detection at every drift rate, including 0.1 m/s — four metres of total offset against a sensor with two metres of noise. Time to detect was a constant 34.2 s at every low rate, which is not how a detector behaves.",
    why:
      "25 s of fault start plus 34.2 s is 59.2 s, and 59.16 s is exactly when the vision sensor goes unavailable on this profile because the aircraft has rolled past the runway. The detection metric was crediting any source leaving the active state, so a geometric certainty was being scored as a detection of a spoof.",
    status:
      "Fixed. Detection is credited only for a source the scenario actually faulted, so a fault-free run cannot produce one at all.",
    reproduce: "aerolab_tests --gtest_filter=Metrics.DetectionIsCreditedOnlyOnTheFaultedSource",
  },
  {
    id: "KF-009",
    title: "The native and WebAssembly builds disagree by 190 m on dead reckoning",
    what:
      "The same source, the same scenario and the same seed produce inertial dead-reckoning results that differ by tens of percent between the two builds: 490 m native against 301 m in WebAssembly. Every other architecture agrees to within centimetres on the same runs.",
    why:
      "Emscripten and glibc disagree in the last bits of the logarithm; IEEE-754 does not specify them. Every Gaussian draw goes through it, and the alignment error is drawn from the very first draws. A filter with an absolute reference contracts that difference back to zero; pure dead reckoning integrates an attitude error as the cube of elapsed time, which turns a last-bit difference into hundreds of metres.",
    status:
      "Measured and published rather than hidden. The parity contract is now stated per dynamics — a metre or 10 % for contracting channels, same order of magnitude for divergent ones — with the divergent numbers printed on every run.",
    reproduce: "node tools/analysis/wasm_parity.mjs",
  },
  {
    id: "KF-001",
    title: "Isolating the satellite source can make the solution worse, not better",
    what:
      "On the dual-fault scenario — a position spoof and an accelerometer bias arriving together — the architecture that gates and isolates ends the run with a HIGHER position error than the plain filter that fuses everything blindly: roughly 25 m RMSE against 16 m.",
    why:
      "Isolation moves the solution onto the inertial channel. When the inertial channel is the other fault, that is a move onto worse data. The architecture is doing the right thing by its own criteria — it declares the result unusable and its availability drops to about 70 % — but the accuracy number alone would suggest the integrity layer is harmful. It is not: it is the only one of the two that tells you not to trust it.",
    status:
      "Open by design. It is a real limit of single-fault reasoning under a double fault, and it is why the scenario asserts the reported MODE rather than the error.",
    reproduce: "aerolab_cli --scenario scenarios/SCN-012.yaml --config configs/evaluation.json",
  },
  {
    id: "KF-008",
    title: "Solution separation added nothing measurable until the cross check was removed",
    what:
      "The detectability sweep showed the two integrity architectures performing almost identically in the default configuration: a floor around 0.5 to 0.75 m/s for both, and 100 % detection above 1 m/s for both. Solution separation, promoted to a first-class architecture specifically because it should beat innovation gating on a slow drift, appeared to buy nothing.",
    why:
      "The detector that was firing was neither of them. A simple threshold on the distance between the satellite fix and the runway-relative vision fix caught the drift first in almost every case. Disabling that cross check while leaving vision feeding the filter separated them sharply — 58 % against 11 % at 0.75 m/s, 94 % against 15 % at 1.0 m/s. Removing vision entirely collapsed both to identical numbers, because the satellite-free sub-filter then has no absolute reference and drifts faster than the spoof does.",
    status:
      "Understood, and the justification revised. Solution separation converts an independent absolute reference into detection power. Given one it lowers the floor by about a factor of six; given none it is an extra filter that reports what the innovation gate already reported.",
    reproduce: "tools/analysis/drift_sweep.sh 150 results/sweep_nocc nocrosscheck",
  },
  {
    id: "KF-002",
    title: "The innovation gate is not blind to a slow drift at this ramp rate",
    what:
      "The slow-drift scenario ramps the satellite position by 150 m over 45 s, about 3.3 m/s. The project's own starting hypothesis was that a chi-square gate on the innovation would miss it. It does not: the gate detects on every seed tested, at around 8.5 s.",
    why:
      "The filter cannot follow the ramp freely, because the barometric and runway-relative vision measurements hold it back, and the cross check against the vision fix fires as well. The structural weakness of innovation gating against slow drift is real, but it only bites below a drift rate this experiment has to find rather than assume.",
    status:
      "The hypothesis as originally stated is not supported at 3.3 m/s. The measured floor is around 0.5 to 0.75 m/s in the default configuration. See KF-008 for what was actually doing the detecting.",
    reproduce: "tools/analysis/drift_sweep.sh",
  },
  {
    id: "KF-004",
    title: "The theoretical solution-separation covariance is optimistic in practice",
    what:
      "Applied literally, the identity cov(x_full − x_sub) = P_sub − P_full produced false isolations on 12 % of fault-free runs, later reduced to 4 % by evaluating the test at the measurement rate instead of every tick.",
    why:
      "The identity holds for two optimal filters. Neither of these is: both are linearised, they share a non-white inertial error, and the main filter may refuse an update its policy rejected. Every one of those effects reduces the cross-covariance, which makes the true covariance of the difference larger than the theory and the statistic biased high.",
    status:
      "Mitigated by a covariance inflation factor of 2.0, calibrated on the tuning seed set and frozen before the evaluation campaign. Zero false isolations over 400 fault-free runs, with unchanged detection rate and time to detect on the fault scenarios. This is the only parameter in the project fitted to measured results.",
    reproduce: "tools/analysis/calibrate_separation.sh",
  },
  {
    id: "KF-006",
    title: "Recovering from a fault can trip the gate a second time",
    what:
      "On the slow-drift scenario, isolations are observed after the fault window has closed, and the metric counts them as false alerts.",
    why:
      "While the source is isolated the solution coasts and drifts. When the source returns to nominal, the honest measurement now disagrees with the drifted filter, so the gate fires again. The alert is not wrong — the two really do disagree — but the cause is the recovery transient, not a new fault.",
    status:
      "Open. Handling it properly means resetting the filter towards the returning source rather than treating its first measurements as suspect; that is a design change, not a threshold change, and it is listed as post-V1 work.",
    reproduce: "aerolab_bench --scenario scenarios/SCN-004.yaml --seeds 1:200 --out results/kf006",
  },
  {
    id: "KF-005",
    title: "The naive baseline is limited by fix age, not by fix noise",
    what:
      "The GNSS-only architecture shows about 12 m of position error on a fault-free approach, with a 2 m satellite noise model. That is six times the noise.",
    why:
      "At 5 Hz with 80 ms of transport latency, the last available fix is on average about 0.18 s old, and the aircraft covers roughly 12.6 m in that time. The error is dominated by the age of the measurement, not by its precision.",
    status:
      "Not a defect. Reported because the number is otherwise easy to misread, and because it is a useful reminder that quoting a sensor's accuracy says little about the accuracy of a navigator built on it.",
    reproduce: "aerolab_cli --scenario scenarios/SCN-001.yaml --config configs/evaluation.json",
  },
];

const fr: FailureEntry[] = [
  {
    id: "KF-003",
    title: "Une gîte discontinue dans la vérité terrain a détruit le filtre",
    what:
      "Sur le profil d’approche avec virage, toutes les architectures dotées d’une couche d’intégrité ont divergé de plusieurs kilomètres sur un vol sans la moindre faute, en isolant trois sources au passage. Le filtre simple, sans couche d’intégrité, allait très bien à 23 m.",
    why:
      "La trajectoire horizontale était une chaîne de segments à courbure constante, donc la courbure sautait à l’entrée de l’arc. Or l’angle de gîte d’un virage coordonné est une fonction de la courbure : la gîte sautait donc aussi, de l’horizontale à 20 degrés en temps nul. C’est une vitesse de roulis infinie. Aucun gyroscope ne peut la rapporter, le filtre n’avait donc aucun moyen de savoir que l’avion avait viré ; il a hérité d’une erreur d’attitude permanente et tout le reste a suivi. Rien n’était faux dans l’estimateur : c’est la vérité terrain qui n’était pas physiquement réalisable.",
    status:
      "Corrigé. L’angle de gîte est maintenant dérivé d’une courbure lissée sur une distance de mise en virage, ce que fait réellement un avion. Un test de non-régression borne les vitesses angulaires sur tout le profil.",
    reproduce: "aerolab_tests --gtest_filter=GroundTruth.BodyRatesStayBoundedThroughTheTurn",
  },
  {
    id: "KF-007",
    title: "Le balayage de détectabilité annonçait des détections qui n’avaient pas eu lieu",
    what:
      "La première exécution du balayage annonçait 100 % de détection à tous les taux de dérive, y compris 0,1 m/s — quatre mètres de décalage total contre un capteur à deux mètres de bruit. Le temps de détection valait constamment 34,2 s à tous les faibles taux, ce qui n’est pas le comportement d’un détecteur.",
    why:
      "25 s de début de faute plus 34,2 s font 59,2 s, et 59,16 s est exactement le moment où le capteur de vision devient indisponible sur ce profil parce que l’avion a dépassé la piste. La métrique de détection créditait n’importe quelle source quittant l’état actif : une certitude géométrique était comptée comme la détection d’une falsification.",
    status:
      "Corrigé. Une détection n’est créditée que pour une source que le scénario a réellement fautée ; un essai sans faute ne peut donc pas en produire.",
    reproduce: "aerolab_tests --gtest_filter=Metrics.DetectionIsCreditedOnlyOnTheFaultedSource",
  },
  {
    id: "KF-009",
    title: "Les builds natif et WebAssembly diffèrent de 190 m en navigation à l’estime",
    what:
      "Le même code source, le même scénario et la même graine produisent des résultats de navigation inertielle à l’estime qui diffèrent de plusieurs dizaines de pour cent entre les deux compilations : 490 m en natif contre 301 m en WebAssembly. Toutes les autres architectures s’accordent au centimètre sur les mêmes essais.",
    why:
      "Emscripten et glibc divergent sur les derniers bits du logarithme ; l’IEEE-754 ne les spécifie pas. Chaque tirage gaussien passe par là, et l’erreur d’alignement est tirée dès les tout premiers tirages. Un filtre disposant d’une référence absolue contracte cette différence vers zéro ; l’estime pure, elle, intègre une erreur d’attitude comme le cube du temps écoulé, ce qui transforme une différence de dernier bit en centaines de mètres.",
    status:
      "Mesuré et publié plutôt que masqué. Le contrat de parité est désormais énoncé par dynamique — un mètre ou 10 % pour les canaux contractants, même ordre de grandeur pour les divergents — avec les chiffres divergents affichés à chaque exécution.",
    reproduce: "node tools/analysis/wasm_parity.mjs",
  },
  {
    id: "KF-001",
    title: "Isoler la source satellite peut dégrader la solution au lieu de l’améliorer",
    what:
      "Sur le scénario à double faute — une falsification de position et un biais accélérométrique arrivant ensemble — l’architecture qui filtre et isole termine l’essai avec une erreur de position PLUS GRANDE que le filtre simple qui fusionne tout aveuglément : environ 25 m de RMSE contre 16 m.",
    why:
      "L’isolation bascule la solution sur le canal inertiel. Quand le canal inertiel est justement l’autre faute, c’est un basculement vers des données pires. L’architecture fait ce qu’il faut selon ses propres critères — elle déclare le résultat inexploitable et sa disponibilité tombe à environ 70 % — mais le seul chiffre de précision suggérerait que la couche d’intégrité est nuisible. Elle ne l’est pas : c’est la seule des deux qui vous dit de ne pas lui faire confiance.",
    status:
      "Ouvert par conception. C’est une limite réelle du raisonnement mono-faute face à une double faute, et c’est pourquoi le scénario porte sur le MODE rapporté et non sur l’erreur.",
    reproduce: "aerolab_cli --scenario scenarios/SCN-012.yaml --config configs/evaluation.json",
  },
  {
    id: "KF-008",
    title:
      "La séparation de solutions n’apportait rien de mesurable tant que le contrôle croisé était actif",
    what:
      "Le balayage de détectabilité montrait les deux architectures d’intégrité presque identiques en configuration par défaut : un plancher autour de 0,5 à 0,75 m/s pour les deux, et 100 % de détection au-delà de 1 m/s pour les deux. La séparation de solutions, promue architecture de plein droit précisément parce qu’elle devait battre le rejet sur innovation face à une dérive lente, semblait ne rien apporter.",
    why:
      "Le détecteur qui se déclenchait n’était ni l’un ni l’autre. Un simple seuil sur la distance entre le point satellite et le point vision relatif à la piste attrapait la dérive en premier dans presque tous les cas. Désactiver ce contrôle croisé tout en laissant la vision alimenter le filtre les a nettement séparés — 58 % contre 11 % à 0,75 m/s, 94 % contre 15 % à 1,0 m/s. Retirer entièrement la vision les a fait s’effondrer à des chiffres identiques, car le sous-filtre sans satellite n’a alors plus de référence absolue et dérive plus vite que la falsification.",
    status:
      "Compris, et la justification révisée. La séparation de solutions convertit une référence absolue indépendante en pouvoir de détection. Avec une telle référence elle abaisse le plancher d’environ un facteur six ; sans elle, c’est un filtre supplémentaire qui rapporte ce que le rejet sur innovation rapportait déjà.",
    reproduce: "tools/analysis/drift_sweep.sh 150 results/sweep_nocc nocrosscheck",
  },
  {
    id: "KF-002",
    title: "Le rejet sur innovation n’est pas aveugle à une dérive lente à ce taux de rampe",
    what:
      "Le scénario de dérive lente fait ramper la position satellite de 150 m en 45 s, soit environ 3,3 m/s. L’hypothèse de départ du projet était qu’un test du khi-deux sur l’innovation la manquerait. Ce n’est pas le cas : le seuil se déclenche sur toutes les graines testées, autour de 8,5 s.",
    why:
      "Le filtre ne peut pas suivre la rampe librement, parce que les mesures barométrique et vision relative à la piste le retiennent, et parce que le contrôle croisé contre le point vision se déclenche aussi. La faiblesse structurelle du rejet sur innovation face à une dérive lente est réelle, mais elle ne mord qu’en dessous d’un taux que cette expérience doit trouver plutôt que supposer.",
    status:
      "L’hypothèse telle qu’énoncée n’est pas soutenue à 3,3 m/s. Le plancher mesuré est autour de 0,5 à 0,75 m/s en configuration par défaut. Voir KF-008 pour ce qui détectait réellement.",
    reproduce: "tools/analysis/drift_sweep.sh",
  },
  {
    id: "KF-004",
    title: "La covariance théorique de la séparation de solutions est optimiste en pratique",
    what:
      "Appliquée littéralement, l’identité cov(x_full − x_sub) = P_sub − P_full produisait de fausses isolations sur 12 % des essais sans faute, ramenées ensuite à 4 % en évaluant le test à la cadence des mesures plutôt qu’à chaque pas de temps.",
    why:
      "L’identité vaut pour deux filtres optimaux. Aucun des deux ne l’est : tous deux sont linéarisés, ils partagent une erreur inertielle non blanche, et le filtre principal peut refuser une mise à jour que sa politique a rejetée. Chacun de ces effets réduit la covariance croisée, ce qui rend la vraie covariance de la différence plus grande que la théorie et la statistique biaisée vers le haut.",
    status:
      "Atténué par un facteur d’inflation de covariance de 2,0, calibré sur le jeu de graines de réglage et gelé avant la campagne d’évaluation. Zéro fausse isolation sur 400 essais sans faute, avec taux de détection et temps de détection inchangés sur les scénarios fautés. C’est le seul paramètre du projet ajusté sur des résultats mesurés.",
    reproduce: "tools/analysis/calibrate_separation.sh",
  },
  {
    id: "KF-006",
    title: "La sortie de faute peut déclencher le seuil une seconde fois",
    what:
      "Sur le scénario de dérive lente, on observe des isolations après la fermeture de la fenêtre de faute, et la métrique les compte comme de fausses alertes.",
    why:
      "Pendant que la source est isolée, la solution avance seule et dérive. Quand la source redevient nominale, la mesure honnête est désormais en désaccord avec le filtre qui a dérivé, et le seuil se redéclenche. L’alerte n’est pas fausse — les deux sont réellement en désaccord — mais la cause est le transitoire de récupération, pas une nouvelle faute.",
    status:
      "Ouvert. Le traiter correctement suppose de recaler le filtre vers la source qui revient plutôt que de traiter ses premières mesures comme suspectes ; c’est un changement de conception, pas de seuil, et c’est listé en travaux post-V1.",
    reproduce: "aerolab_bench --scenario scenarios/SCN-004.yaml --seeds 1:200 --out results/kf006",
  },
  {
    id: "KF-005",
    title: "La référence naïve est limitée par l’âge du point, pas par son bruit",
    what:
      "L’architecture GNSS seul affiche environ 12 m d’erreur de position sur une approche sans faute, avec un modèle de bruit satellite à 2 m. Soit six fois le bruit.",
    why:
      "À 5 Hz avec 80 ms de latence de transport, le dernier point disponible a en moyenne environ 0,18 s d’âge, et l’avion parcourt à peu près 12,6 m pendant ce temps. L’erreur est dominée par l’âge de la mesure, pas par sa précision.",
    status:
      "Ce n’est pas un défaut. Rapporté parce que le chiffre est sinon facile à mal lire, et parce que c’est un rappel utile : citer la précision d’un capteur ne dit presque rien de la précision d’un navigateur bâti dessus.",
    reproduce: "aerolab_cli --scenario scenarios/SCN-001.yaml --config configs/evaluation.json",
  },
];

export const FAILURE_ENTRIES: Record<Lang, FailureEntry[]> = { fr, en };
