// AEROLAB RESILIENCE - the explanatory body of the overview page, FR and EN.
//
// Kept in its own module for the same reason as the failure catalogue: it is
// long prose, it changes on its own schedule, and burying it in strings.ts makes
// that file hard to read for the hundred short labels that belong there.
//
// It does not restate what the tool IS: the hero above it on the overview page
// already does that, and saying it twice is how a landing page starts to feel
// padded.
import type { Lang } from "./strings";

export interface GuideStrings {
  h_why: string;
  p_why: string[];
  h_how: string;
  p_how: string;
  steps: Array<{ title: string; text: string }>;
  h_read: string;
  p_read: string;
  panels: Array<{ name: string; text: string }>;
  h_limits: string;
  p_limits: string[];
  h_learned: string;
  findings: Array<{ title: string; text: string }>;
  h_motivation: string;
  p_motivation: string[];
  next: string;
  start: string;
}

const en: GuideStrings = {
  h_why: "The problem it addresses",
  p_why: [
    "An aircraft does not know where it is. It estimates: it combines an inertial unit, a satellite fix, a barometer and, on approach, a runway-relative measurement, then publishes a position with an uncertainty attached. That works well right up until one of those sources starts lying.",
    "Satellite navigation interference has been the subject of published safety bulletins and coordinated action plans from European aviation authorities, and resilient positioning, navigation and timing is an active industrial programme area. The interesting question is not whether it happens. It is what an integrity architecture actually buys you when it does, and how much.",
    "You cannot answer that by breaking a real aircraft's navigation. You answer it on a bench where the truth is known exactly, where the fault is exactly the one you injected, and where the run repeats bit for bit.",
  ],

  h_how: "How it works",
  p_how:
    "Five stages, in this order. The separation between them is the whole design: each one sees only what the stage before it hands over.",
  steps: [
    {
      title: "A trajectory, in closed form",
      text: "The approach is computed analytically rather than integrated. It carries no state, so it cannot drift, and it is the reference every error on this site is measured against. No estimator ever receives it.",
    },
    {
      title: "Synthetic sensors",
      text: "Each sensor reads that trajectory and adds its own noise, bias, latency and delivery cadence. Sample time and delivery time are kept separate, because a measurement that arrives late is a different failure from a measurement that is wrong.",
    },
    {
      title: "A fault engine",
      text: "It applies arithmetic to the measurements: an offset, a slow ramp, a frozen value, a burst of noise, a silence. It is never given the trajectory — enforced by the signature of the function rather than by convention, and covered by its own acceptance test.",
    },
    {
      title: "Five architectures, one stream",
      text: "All five consume the identical resulting measurement stream, tick for tick. Any difference between their results is therefore attributable to the architecture rather than to luck.",
    },
    {
      title: "Metrics, as distributions",
      text: "Time to detect, time to isolate, availability, false-alert rate and filter consistency, over a thousand seeds per scenario. Reported as distributions, because the mean of a detection time hides the case that matters.",
    },
  ],

  h_read: "Reading the Live Lab",
  p_read: "What each part of the screen is telling you, and what is worth watching.",
  panels: [
    {
      name: "The 3D view",
      text: "The aircraft is the simulation truth. Each coloured bead is where one architecture believes it is, and the thick line between them is the error itself. A bead that sinks under the ground has diverged: that is not a position an aircraft can occupy.",
    },
    {
      name: "The legend",
      text: "Colour, architecture and current error, in a fixed corner. The colours are the ones the solutions table and the chart use.",
    },
    {
      name: "Sensor health",
      text: "Per source: its state, the age of its last measurement, and its Normalized Innovation Squared against the gate it is judged on. The state words are the identifiers the engine emits and the telemetry records — never translated, so that the screen and the files agree.",
    },
    {
      name: "Solutions",
      text: "Error against truth, the uncertainty each filter claims for itself, and its navigation mode. Watch the gap: a filter whose claimed sigma stays small while its true error grows is overconfident, and closing that gap is what an integrity layer is for.",
    },
    {
      name: "Position error chart",
      text: "Every architecture on one logarithmic axis, with the injected fault window shaded. Move the pointer across it to read every value at one instant, or click to pin that instant.",
    },
    {
      name: "Integrity and injected faults",
      text: "Every state change, in order, with the reason it was taken and the statistic it was decided on. On a nominal run this list should stay empty; anything in it is a false alert.",
    },
    {
      name: "Report",
      text: "Produced when a run finishes: the metrics, the acceptance verdict, and the provenance — commit, compiler, and the content hash of the scenario and configuration that produced it.",
    },
  ],

  h_limits: "What it is not",
  p_limits: [
    "Simulation only. Everything called a spoof, a jam or an attack here is a number added to an array inside this one page. There is no radio, no signal generator, no receiver model, no transmission parameter and no operational procedure for interfering with a real system anywhere in the source.",
    "Not a certified system. No conformance to DO-178C, RAIM, ARAIM or any other standard is claimed, the residual test is educational, and no protection level is computed.",
    "The simplifications are stated rather than hidden: flat earth, no wind, zero angle of attack, a frozen constellation geometry. They are listed in full on the Methodology page.",
  ],

  h_learned: "What came out of it",
  findings: [
    {
      title: "A detectability floor, not a yes or no",
      text: "Below roughly 0.5 to 0.75 m/s of injected drift, none of the architectures here detects reliably. The number is the result; the sweep that found it, and the seed sets kept apart for tuning and for evaluation, are the method.",
    },
    {
      title: "Solution separation bought less than expected",
      text: "In the default configuration the two integrity policies were indistinguishable, because a plain cross check against the runway-relative fix was catching the drift first and both inherited its performance. Disable that check and they separate by a factor of six. Remove the vision sensor entirely and both collapse to the same numbers. Solution separation converts an independent absolute reference into detection power; it does not create integrity out of nothing.",
    },
    {
      title: "The bench caught its own simulator",
      text: "Three of the published failures were found by the platform catching its own trajectory generator or its own metrics — including a detection metric that reported 100 % success at a drift rate no detector could possibly have seen. That is what a test bench is for, and it is why the failures are published rather than quietly fixed.",
    },
  ],

  h_motivation: "Why it was built",
  p_motivation: [
    "Because a claim about an integrity architecture is worth measuring rather than repeating. Every figure on this site has a scenario file, a seed and a content hash behind it, and every result that contradicted the project's own expectations is published in the failure catalogue instead of being quietly dropped.",
    "And because the honest way to show engineering work is to let someone run it, break it, and read the numbers for themselves. That is what this page is for.",
  ],

  next: "Then: pick a scenario and break something.",
  start: "Choose a scenario",
};

const fr: GuideStrings = {
  h_why: "Le problème qu’il traite",
  p_why: [
    "Un avion ne sait pas où il est. Il l’estime : il combine une centrale inertielle, un point satellite, un baromètre et, en approche, une mesure relative à la piste, puis publie une position assortie d’une incertitude. Ça fonctionne très bien — jusqu’au moment où l’une de ces sources se met à mentir.",
    "Les interférences sur la navigation par satellite font l’objet de bulletins de sécurité publiés et de plans d’action coordonnés des autorités européennes de l’aviation, et le PNT résilient est un axe de programme industriel actif. La question intéressante n’est pas de savoir si ça arrive, mais ce qu’une architecture d’intégrité apporte réellement quand ça arrive, et dans quelle mesure.",
    "On ne répond pas à ça en cassant la navigation d’un avion réel. On y répond sur un banc où la vérité est connue exactement, où la faute est exactement celle qu’on a injectée, et où l’essai se rejoue au bit près.",
  ],

  h_how: "Comment ça fonctionne",
  p_how:
    "Cinq étages, dans cet ordre. Leur cloisonnement est tout le principe : chacun ne voit que ce que le précédent lui transmet.",
  steps: [
    {
      title: "Une trajectoire en forme close",
      text: "L’approche est calculée analytiquement plutôt qu’intégrée. Elle ne porte aucun état, donc elle ne peut pas dériver, et c’est la référence à laquelle toutes les erreurs du site sont comparées. Aucun estimateur ne la reçoit jamais.",
    },
    {
      title: "Des capteurs synthétiques",
      text: "Chaque capteur lit cette trajectoire et y ajoute son bruit, son biais, sa latence et sa cadence de livraison. L’instant d’échantillonnage et l’instant de livraison sont distingués, parce qu’une mesure qui arrive en retard est une panne différente d’une mesure qui est fausse.",
    },
    {
      title: "Un moteur de fautes",
      text: "Il applique de l’arithmétique aux mesures : un décalage, une rampe lente, une valeur figée, une bouffée de bruit, un silence. Il ne reçoit jamais la trajectoire — c’est imposé par la signature de la fonction, pas par convention, et couvert par son propre test d’acceptation.",
    },
    {
      title: "Cinq architectures, un seul flux",
      text: "Les cinq consomment le flux de mesures identique, pas de temps par pas de temps. Toute différence entre leurs résultats est donc imputable à l’architecture, et pas à la chance.",
    },
    {
      title: "Des métriques, sous forme de distributions",
      text: "Temps de détection, temps d’isolation, disponibilité, taux de fausse alerte et cohérence du filtre, sur mille graines par scénario. Publiées en distributions, parce que la moyenne d’un temps de détection masque justement le cas qui compte.",
    },
  ],

  h_read: "Lire le Labo en direct",
  p_read: "Ce que chaque partie de l’écran vous dit, et ce qui mérite d’être surveillé.",
  panels: [
    {
      name: "La vue 3D",
      text: "L’avion est la vérité de simulation. Chaque perle colorée est là où une architecture croit se trouver, et le trait épais qui les relie est l’erreur elle-même. Une perle qui passe sous le sol a décroché : ce n’est pas une position qu’un avion peut occuper.",
    },
    {
      name: "La légende",
      text: "Couleur, architecture et erreur courante, dans un coin fixe. Ce sont les couleurs qu’utilisent aussi le tableau des solutions et le graphique.",
    },
    {
      name: "État des capteurs",
      text: "Par source : son état, l’âge de sa dernière mesure, et son NIS face au seuil sur lequel on la juge. Les mots d’état sont les identifiants émis par le moteur et enregistrés dans la télémétrie — jamais traduits, pour que l’écran et les fichiers concordent.",
    },
    {
      name: "Solutions",
      text: "L’erreur face à la vérité, l’incertitude que chaque filtre revendique, et son mode de navigation. Surveillez l’écart : un filtre dont le sigma déclaré reste petit alors que son erreur réelle grandit est trop confiant, et refermer cet écart est précisément le rôle d’une couche d’intégrité.",
    },
    {
      name: "Graphique d’erreur de position",
      text: "Toutes les architectures sur un axe logarithmique, avec la fenêtre de faute injectée grisée. Promenez le pointeur pour lire toutes les valeurs à un instant donné, ou cliquez pour épingler cet instant.",
    },
    {
      name: "Intégrité et fautes injectées",
      text: "Chaque changement d’état, dans l’ordre, avec la raison retenue et la statistique sur laquelle la décision s’est prise. Sur un vol nominal ce journal doit rester vide : tout ce qui s’y trouve est une fausse alerte.",
    },
    {
      name: "Rapport",
      text: "Produit à la fin d’un essai : les métriques, le verdict d’acceptation, et la provenance — commit, compilateur, et l’empreinte du scénario et de la configuration qui l’ont produit.",
    },
  ],

  h_limits: "Ce que ce n’est pas",
  p_limits: [
    "De la simulation, uniquement. Tout ce qui est appelé ici falsification, brouillage ou attaque est un nombre ajouté à un tableau, à l’intérieur de cette seule page. Il n’y a nulle part dans le code de radio, de générateur de signal, de modèle de récepteur, de paramètre d’émission ni de procédure opérationnelle permettant d’interférer avec un système réel.",
    "Ce n’est pas un système certifié. Aucune conformité à DO-178C, RAIM, ARAIM ou à une autre norme n’est revendiquée, le test de résidus est pédagogique, et aucun niveau de protection n’est calculé.",
    "Les simplifications sont annoncées plutôt que cachées : Terre plane, pas de vent, incidence nulle, géométrie de constellation figée. Elles sont énumérées en entier sur la page Méthodologie.",
  ],

  h_learned: "Ce qui en est sorti",
  findings: [
    {
      title: "Un plancher de détectabilité, pas un oui ou non",
      text: "En dessous d’environ 0,5 à 0,75 m/s de dérive injectée, aucune des architectures présentes ne détecte de façon fiable. Le chiffre est le résultat ; le balayage qui l’a trouvé, et la séparation des jeux de graines entre réglage et évaluation, sont la méthode.",
    },
    {
      title: "La séparation de solutions a rapporté moins que prévu",
      text: "En configuration par défaut, les deux politiques d’intégrité étaient indiscernables : un simple contrôle croisé contre le point vision relatif à la piste attrapait la dérive en premier, et les deux héritaient de sa performance. Désactivez ce contrôle et elles se séparent d’un facteur six. Retirez entièrement le capteur de vision et les deux s’effondrent aux mêmes chiffres. La séparation de solutions convertit une référence absolue indépendante en pouvoir de détection ; elle ne fabrique pas de l’intégrité à partir de rien.",
    },
    {
      title: "Le banc a pris son propre simulateur en défaut",
      text: "Trois des échecs publiés ont été trouvés par la plateforme elle-même, en prenant en défaut son générateur de trajectoire ou ses propres métriques — dont une métrique de détection qui annonçait 100 % de réussite à un taux de dérive qu’aucun détecteur n’aurait pu voir. C’est à ça que sert un banc d’essai, et c’est pour ça que les échecs sont publiés plutôt que corrigés en silence.",
    },
  ],

  h_motivation: "Pourquoi ce projet",
  p_motivation: [
    "Parce qu’une affirmation sur une architecture d’intégrité mérite d’être mesurée plutôt que répétée. Chaque chiffre de ce site a derrière lui un fichier de scénario, une graine et une empreinte de contenu ; et chaque résultat qui a contredit les attentes du projet lui-même est publié dans le catalogue d’échecs au lieu d’être discrètement écarté.",
    "Et parce que la façon honnête de montrer un travail d’ingénierie, c’est de laisser quelqu’un le lancer, le casser et lire les chiffres lui-même. C’est à ça que sert cette page.",
  ],

  next: "Ensuite : choisissez un scénario et cassez quelque chose.",
  start: "Choisir un scénario",
};

export const GUIDE: Record<Lang, GuideStrings> = { fr, en };
