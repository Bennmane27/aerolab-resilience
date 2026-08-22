// AEROLAB RESILIENCE - explanatory commentary for the Live Lab.
//
// The first version of this was a translation of the screen: it said "X flagged
// GNSS 0.3 s after the injection", which is what the event log already said in
// its own vocabulary. Reading it told you nothing you could not see.
//
// This version explains the MECHANISM. Every integrity event carries the engine's
// reason code - which check actually fired - and every fault carries its type, so
// the commentary can say why that particular test was the one that caught it,
// what it costs, and where the same test is known to fail. The interesting
// sentences are the ones about the limits: an architecture that catches a 100 m
// jump has demonstrated almost nothing, and saying so is the point.
import type { Lang } from "./strings";

export interface Explained {
  /** What happened, in one clause. */
  what: string;
  /** Why it happened that way, and what it tells you. This is the payload. */
  why: string;
}

export interface Narration {
  title: string;
  objectiveLabel: string;
  watchLabel: string;
  historyHint: string;
  empty: string;

  /** Opening entry, before anything is injected. */
  opening: (fault: string, target: string, seconds: string) => Explained;
  /** A scenario that injects nothing at all. */
  openingNominal: Explained;
  /** A fault has been armed. Keyed on fault type by `faultMechanism`. */
  faultArmed: (fault: string, target: string) => Explained;
  faultEnded: (target: string) => Explained;
  /** An architecture changed a source's state. Keyed on reason code. */
  reaction: (architecture: string, target: string, state: string, seconds: string) => Explained;
  /** Nothing has reacted yet, some way into the injection. */
  silence: (seconds: string) => Explained;
  finished: (bestName: string, best: string, worstName: string, worst: string) => Explained;
  belowGround: (architecture: string) => Explained;

  /** What each fault type actually does, and why it is hard or easy. */
  faultMechanism: Record<string, string>;
  /** What each engine reason code means, and what firing it proves. */
  reasonMechanism: Record<string, string>;
}

const en: Narration = {
  title: "What is happening",
  objectiveLabel: "This scenario",
  watchLabel: "Watch",
  historyHint: "scroll for the earlier entries",
  empty: "Waiting for the first measurements.",

  opening: (fault, target, seconds) => ({
    what: `Nominal approach. ${fault} arrives on ${target} in ${seconds} s.`,
    why: "Right now every source agrees and the five architectures sit within a few metres of each other. That agreement is the baseline: it is what makes any later disagreement attributable to the fault rather than to noise. Note how close they are, because in a moment they will stop being close and the size of the gap is the result.",
  }),

  openingNominal: {
    what: "Nominal approach. Nothing will be injected.",
    why: "This is the control case, and it measures the cost of having an integrity layer at all. Every alarm raised here is a false alarm, and a false alarm is not free: it throws away a source that was working, and it is what a real crew learns to ignore. The threshold that decides these alarms is derived from a stated probability of false alert, so this scenario is how that stated probability is checked against reality.",
  },

  faultArmed: (fault, target) => ({
    what: `${fault} is now applied to ${target}.`,
    why: "The trajectory has not moved. Only what the aircraft is told about it has. The fault engine never receives the true trajectory — that is enforced by the signature of the function, not by convention — so what follows is entirely a question of what the architectures can work out from measurements that disagree with each other.",
  }),

  faultEnded: (target) => ({
    what: `The injection on ${target} has stopped.`,
    why: "The source is nominal again, which starts a second, quieter test. An architecture that isolated it must now decide when to trust it again. Too eager and the next transient throws the source away again; too slow and a healthy sensor sits unused through the part of the approach where it matters most. The recovery window that governs this is a tuned parameter, not a natural constant.",
  }),

  reaction: (architecture, target, state, seconds) => ({
    what: `${architecture} moved ${target} to ${state}, ${seconds} s after the injection.`,
    why: "",
  }),

  silence: (seconds) => ({
    what: `${seconds} s into the injection, nothing has reacted.`,
    why: "Silence here is a measurement, not an omission. Either the fault is still small compared with what the filter expects its measurements to disagree by, or it is the kind of fault the tests in use are structurally unable to see. The dead reckoning and GNSS-only solutions have no opinion in either case: neither has a second source to disagree with.",
  }),

  finished: (bestName, best, worstName, worst) => ({
    what: `Run complete. ${bestName} ended at ${best} m, ${worstName} at ${worst} m.`,
    why: "Both consumed exactly the same measurements, tick for tick, so the whole of that difference belongs to the architecture. One run on one seed is still an anecdote: the published figures are distributions over a thousand seeds per scenario, and the report page carries the hash of the scenario and configuration that produced this one.",
  }),

  belowGround: (architecture) => ({
    what: `${architecture} now places the aircraft below the ground.`,
    why: "That is not a large error, it is an impossible position, and the distinction matters. A solution that has crossed into the impossible has stopped being a degraded estimate and become a diverged one — the filter is no longer tracking anything, it is integrating its own mistake. Any protection level it reports at this point is meaningless.",
  }),

  faultMechanism: {
    gnss_position_step:
      "A sudden jump is the easy case, and it is worth being clear about how easy. The filter predicted a position and claims an uncertainty of a couple of metres; a fix a hundred metres away is tens of standard deviations out, so a chi-square gate rejects it on the very first measurement. An architecture that catches this has demonstrated that its gate is wired up, and almost nothing else. The hard case is the slow drift.",
    gnss_position_ramp:
      "A slow ramp is the case that defeats innovation gating, and it does so structurally rather than by being large. Each measurement is only slightly inconsistent with the prediction, so no single one trips the gate — and worse, the filter absorbs each small inconsistency into its own state, so the next prediction is already wrong in the same direction and the residual stays small. The filter follows the lie. This is the whole reason solution separation exists: a sub-filter that never receives satellite data cannot follow it, so the gap between the two solutions grows with the injected error instead of being absorbed by it.",
    source_unavailable:
      "Nothing is lying here; the source simply stops. Detection is trivial — the absence is the signal — so the interesting quantity is continuity. How fast does the error grow once the only absolute position reference is gone, and does each filter's declared uncertainty grow honestly alongside it? A filter that keeps claiming two metres while coasting on inertial data alone is the failure worth catching.",
    freeze:
      "The value stays perfectly plausible: it is a position the aircraft genuinely occupied a moment ago. Nothing about the number is wrong, so nothing that examines the number can find anything. Only its age betrays it, which is why the sample timestamp is tracked separately from the delivery timestamp and why a repeated sequence number is its own reason code.",
    latency:
      "The measurement is correct and arrives late. Applied at the wrong instant it injects an error of roughly speed times delay — at approach speed, a second and a half of delay is a hundred metres. Inside the buffer the filter rolls back and reprocesses it at its true sample time; beyond the buffer it has to be refused as stale, because using it would be worse than not having it.",
    noise_burst:
      "The measurements stay unbiased but become far noisier than the model says they should be. The position error may not grow much; what breaks is consistency. A filter that keeps its modelled noise becomes overconfident — its true error grows while the uncertainty it declares does not — and that gap, not the error, is what an integrity claim rests on.",
    imu_accel_bias:
      "An accelerometer bias is integrated twice, so the position error grows with the square of time: quietly at first, then not. With an absolute reference the filter can observe the bias and estimate it out. Without one, nothing bounds it, which is exactly what the dead-reckoning solution is here to show.",
    imu_gyro_bias:
      "A gyroscope bias tilts the estimated attitude, which mis-projects gravity into the horizontal channel, which is then integrated twice. The position error grows with the cube of time. It is the most expensive inertial fault and the least visible at the moment it starts.",
    vision_degrade:
      "The source keeps publishing and tells the truth about its own decline: its declared quality falls. This tests whether an architecture believes that declaration and down-weights accordingly, or keeps leaning on a source that has explicitly said not to. Degrading is not the same as failing, and treating them the same throws away information.",
    gnss_velocity_inconsistency:
      "The position stays plausible and only the velocity is wrong. A gate that looks at position residuals has nothing to fire on — the positions agree. Catching this needs a check across channels, and it needs its own reason code, because 'the position looks fine' and 'the source is healthy' are different statements.",
    pseudorange_outlier:
      "One satellite carries a range bias. With enough satellites the pattern of residuals identifies which one, so the offending measurement can be excluded while the source as a whole keeps being used. That is the difference between excluding a satellite and excluding GNSS, and it only exists while the redundancy does.",
  },

  reasonMechanism: {
    NIS_ABOVE_THRESHOLD:
      "The innovation gate fired. The filter compared the measurement against its own prediction and divided the disagreement by the uncertainty it claims for that comparison; the result exceeded a chi-square threshold derived from a stated probability of false alert. This test is strong against anything abrupt and structurally weak against anything slow.",
    NIS_PERSISTENT:
      "The disagreement persisted. A single outlier is not evidence of a broken source — measurements are noisy and rejecting one is normal — so the policy requires the inconsistency to hold for a number of consecutive measurements before it will suspect the source. That counter is what separates a detector from a twitch.",
    CROSS_CHECK_VISION:
      "The satellite fix and the runway-relative vision fix disagree about where the aircraft is. This is a plain distance threshold between two independent absolute references, and it is the check the project's own measurements showed was doing most of the work: with it enabled, innovation gating and solution separation were indistinguishable, because this fired first and both inherited its result. Disable it and they separate by a factor of six.",
    CROSS_CHECK_INERTIAL:
      "The satellite fix disagrees with where the inertial solution says the aircraft should be. The inertial solution drifts, but it drifts smoothly and it cannot be spoofed from outside, so over a short window it is a useful witness against a source that has jumped.",
    SOLUTION_SEPARATION:
      "The all-sources solution has drifted away from a sub-filter that never receives satellite data, by more than their combined uncertainty allows. This is the test built for slow drift: the sub-filter cannot follow the lie, so the gap grows with the injected error instead of being absorbed into the state. It buys detection power only where an independent absolute reference exists — remove the vision sensor and it collapses back to the ordinary gate.",
    MEASUREMENT_STALE:
      "The sample is older than the freshness limit. Nothing about the value is wrong; it was true when it was taken. This is why sample time and delivery time are carried separately through the whole measurement bus.",
    SEQUENCE_REPEATED:
      "The source is republishing a sample it has already sent. A frozen sensor produces perfectly plausible numbers indefinitely, so the sequence number is what catches it rather than any test on the value.",
    SOURCE_UNAVAILABLE:
      "The source stopped delivering usable measurements. This is an observation rather than a decision — nothing was excluded, there is simply nothing arriving — and it is reported differently for that reason.",
    SOURCE_RETURNED: "The source is delivering again. Whether it is trusted again is a separate decision, governed by the recovery window.",
    NIS_NORMAL_CLEARED:
      "Residuals have been normal long enough for the source to be trusted again. The hysteresis here is deliberate: clearing on the first good measurement would let a source flicker in and out of the solution.",
    RECOVERY_WINDOW_ELAPSED:
      "The recovery window has run its course and the source is consistent again. The length of that window is a tuned parameter and a real trade-off: it is time spent flying without a source that may already be healthy.",
    QUALITY_BELOW_THRESHOLD:
      "The source declared its own quality too low to use. Believing that declaration is cheaper and more honest than inferring the same thing from residuals several seconds later.",
    VELOCITY_INCONSISTENT:
      "The reported velocity does not agree with the rest of the solution even though the position looks plausible. Position residuals alone cannot express this, which is why it has a reason code of its own.",
    REDUNDANCY_INSUFFICIENT:
      "Not enough independent sources are left to support an integrity claim. The correct behaviour at this point is to say so — declaring low confidence — rather than to keep publishing a position with a protection level nothing supports.",
    INNOVATION_COVARIANCE_INVALID:
      "The innovation covariance was not usable, so the update was refused rather than applied blind. Refusing is the safe direction: a bad update is harder to recover from than a missing one.",
    MANUAL_ISOLATION: "Isolated on an operator request rather than by a test.",
    NONE: "No reason code: the source is nominal.",
  },
};

const fr: Narration = {
  title: "Ce qui se passe",
  objectiveLabel: "Ce scénario",
  watchLabel: "À surveiller",
  historyHint: "faites défiler pour les entrées précédentes",
  empty: "En attente des premières mesures.",

  opening: (fault, target, seconds) => ({
    what: `Approche nominale. ${fault} arrive sur ${target} dans ${seconds} s.`,
    why: "Pour l’instant toutes les sources concordent et les cinq architectures tiennent à quelques mètres les unes des autres. Cette concordance est la référence : c’est elle qui permettra d’imputer tout désaccord ultérieur à la faute et non au bruit. Retenez à quel point elles sont proches, parce que dans un instant elles cesseront de l’être, et l’ampleur de l’écart est le résultat.",
  }),

  openingNominal: {
    what: "Approche nominale. Rien ne sera injecté.",
    why: "C’est le cas témoin, et il mesure ce que coûte le fait même d’avoir une couche d’intégrité. Toute alarme levée ici est une fausse alarme, et une fausse alarme n’est pas gratuite : elle jette une source qui fonctionnait, et c’est ce qu’un équipage réel apprend à ignorer. Le seuil qui décide de ces alarmes est dérivé d’une probabilité de fausse alerte annoncée ; ce scénario est précisément la façon dont cette probabilité annoncée est confrontée aux faits.",
  },

  faultArmed: (fault, target) => ({
    what: `${fault} s’applique désormais à ${target}.`,
    why: "La trajectoire n’a pas bougé. Seul ce qu’on en raconte à l’avion a changé. Le moteur de fautes ne reçoit jamais la trajectoire réelle — c’est imposé par la signature de la fonction, pas par convention — donc tout ce qui suit relève uniquement de ce que les architectures peuvent déduire de mesures qui se contredisent.",
  }),

  faultEnded: (target) => ({
    what: `L’injection sur ${target} est terminée.`,
    why: "La source est redevenue nominale, ce qui ouvre un second test, plus discret. Une architecture qui l’avait isolée doit maintenant décider quand lui refaire confiance. Trop tôt, et le prochain transitoire la fera rejeter de nouveau ; trop tard, et un capteur sain reste inutilisé pendant la partie de l’approche où il compte le plus. La fenêtre de récupération qui régit cela est un paramètre réglé, pas une constante naturelle.",
  }),

  reaction: (architecture, target, state, seconds) => ({
    what: `${architecture} fait passer ${target} en ${state}, ${seconds} s après l’injection.`,
    why: "",
  }),

  silence: (seconds) => ({
    what: `${seconds} s après l’injection, rien n’a réagi.`,
    why: "Ce silence est une mesure, pas un oubli. Soit la faute reste petite devant l’écart que le filtre s’attend de toute façon à voir entre ses mesures, soit c’est le type de faute que les tests en place sont structurellement incapables de voir. La navigation à l’estime et le GNSS seul n’ont d’avis ni dans un cas ni dans l’autre : ni l’une ni l’autre n’a de seconde source avec quoi se contredire.",
  }),

  finished: (bestName, best, worstName, worst) => ({
    what: `Essai terminé. ${bestName} finit à ${best} m, ${worstName} à ${worst} m.`,
    why: "Les deux ont consommé exactement les mêmes mesures, pas de temps par pas de temps : la totalité de cet écart revient donc à l’architecture. Un essai sur une graine reste une anecdote — les chiffres publiés sont des distributions sur mille graines par scénario, et la page Rapport porte l’empreinte du scénario et de la configuration qui ont produit celui-ci.",
  }),

  belowGround: (architecture) => ({
    what: `${architecture} place maintenant l’avion sous le sol.`,
    why: "Ce n’est pas une grande erreur, c’est une position impossible, et la distinction compte. Une solution passée dans l’impossible a cessé d’être une estimation dégradée pour devenir une estimation qui a décroché : le filtre ne suit plus rien, il intègre sa propre erreur. Le niveau de protection qu’il annonce à ce stade ne veut plus rien dire.",
  }),

  faultMechanism: {
    gnss_position_step:
      "Un saut brutal est le cas facile, et il vaut la peine de dire à quel point. Le filtre a prédit une position et revendique une incertitude de quelques mètres ; un point situé cent mètres plus loin est à des dizaines d’écarts-types, donc un test du khi-deux le rejette dès la toute première mesure. Une architecture qui attrape ça a démontré que sa barrière est branchée, et à peu près rien d’autre. Le cas difficile, c’est la dérive lente.",
    gnss_position_ramp:
      "Une rampe lente est le cas qui met en défaut le rejet sur innovation, et elle le fait structurellement, pas par son amplitude. Chaque mesure n’est que légèrement incohérente avec la prédiction, donc aucune ne franchit le seuil à elle seule — et pire, le filtre absorbe chaque petite incohérence dans son propre état, si bien que la prédiction suivante est déjà fausse dans le même sens et que le résidu reste petit. Le filtre suit le mensonge. C’est toute la raison d’être de la séparation de solutions : un sous-filtre qui ne reçoit jamais de données satellite ne peut pas le suivre, donc l’écart entre les deux solutions croît avec l’erreur injectée au lieu d’être absorbé par elle.",
    source_unavailable:
      "Ici personne ne ment ; la source s’arrête, tout simplement. La détection est triviale — l’absence est le signal — donc la vraie question est la continuité. À quelle vitesse l’erreur croît-elle une fois la seule référence de position absolue disparue, et l’incertitude déclarée par chaque filtre croît-elle honnêtement avec elle ? Un filtre qui continue d’annoncer deux mètres alors qu’il avance sur l’inertiel seul est la défaillance qu’il faut attraper.",
    freeze:
      "La valeur reste parfaitement plausible : c’est une position que l’avion occupait réellement il y a un instant. Rien n’est faux dans le nombre, donc rien de ce qui examine le nombre ne peut trouver quoi que ce soit. Seul son âge la trahit — d’où le suivi séparé de l’horodatage d’échantillonnage et de celui de livraison, et d’où un code de raison propre à la répétition de séquence.",
    latency:
      "La mesure est correcte et arrive en retard. Appliquée au mauvais instant, elle injecte une erreur de l’ordre de la vitesse multipliée par le retard : à vitesse d’approche, une seconde et demie de retard fait cent mètres. Dans la fenêtre du tampon, le filtre revient en arrière et la retraite à son vrai instant d’échantillonnage ; au-delà, elle doit être refusée comme périmée, parce que l’utiliser serait pire que de s’en passer.",
    noise_burst:
      "Les mesures restent non biaisées mais deviennent bien plus bruitées que le modèle ne le prévoit. L’erreur de position peut à peine bouger ; ce qui casse, c’est la cohérence. Un filtre qui conserve son bruit modélisé devient trop confiant — son erreur réelle grandit alors que l’incertitude qu’il déclare, non — et c’est cet écart, et non l’erreur, qui fonde une revendication d’intégrité.",
    imu_accel_bias:
      "Un biais accélérométrique est intégré deux fois : l’erreur de position croît donc comme le carré du temps, discrètement d’abord, puis plus du tout. Avec une référence absolue, le filtre peut observer ce biais et l’estimer. Sans elle, rien ne le borne — et c’est précisément ce que la solution à l’estime est là pour montrer.",
    imu_gyro_bias:
      "Un biais gyrométrique incline l’attitude estimée, ce qui projette mal la gravité dans le canal horizontal, lequel est ensuite intégré deux fois. L’erreur de position croît comme le cube du temps. C’est la faute inertielle la plus coûteuse, et la moins visible à l’instant où elle commence.",
    vision_degrade:
      "La source continue de publier et dit la vérité sur son propre déclin : la qualité qu’elle déclare baisse. Cela teste si une architecture croit cette déclaration et pondère en conséquence, ou si elle continue de s’appuyer sur une source qui lui a explicitement dit de ne pas le faire. Se dégrader n’est pas défaillir, et traiter les deux de la même façon jette de l’information.",
    gnss_velocity_inconsistency:
      "La position reste plausible, seule la vitesse est fausse. Une barrière qui regarde les résidus de position n’a rien sur quoi se déclencher : les positions concordent. Attraper cela demande un contrôle entre canaux, et cela mérite un code de raison propre, parce que « la position a l’air correcte » et « la source est saine » ne sont pas la même affirmation.",
    pseudorange_outlier:
      "Un satellite porte un biais de distance. Avec assez de satellites, la structure des résidus désigne lequel : la mesure fautive peut être exclue pendant que la source dans son ensemble continue d’être utilisée. C’est toute la différence entre exclure un satellite et exclure le GNSS — et elle n’existe que tant que la redondance existe.",
  },

  reasonMechanism: {
    NIS_ABOVE_THRESHOLD:
      "La barrière sur l’innovation s’est déclenchée. Le filtre a comparé la mesure à sa propre prédiction et divisé le désaccord par l’incertitude qu’il revendique pour cette comparaison ; le résultat a dépassé un seuil du khi-deux dérivé d’une probabilité de fausse alerte annoncée. Ce test est puissant face à tout ce qui est brutal, et structurellement faible face à tout ce qui est lent.",
    NIS_PERSISTENT:
      "Le désaccord a persisté. Une valeur aberrante isolée ne prouve pas qu’une source est cassée — les mesures sont bruitées et en rejeter une est normal — donc la politique exige que l’incohérence tienne sur plusieurs mesures consécutives avant de suspecter la source. Ce compteur est ce qui sépare un détecteur d’un réflexe nerveux.",
    CROSS_CHECK_VISION:
      "Le point satellite et le point vision relatif à la piste sont en désaccord sur la position de l’avion. C’est un simple seuil de distance entre deux références absolues indépendantes, et c’est le contrôle dont les mesures du projet ont montré qu’il faisait l’essentiel du travail : avec lui actif, rejet sur innovation et séparation de solutions étaient indiscernables, parce que c’est lui qui se déclenchait en premier et que les deux héritaient de son résultat. Désactivez-le et elles se séparent d’un facteur six.",
    CROSS_CHECK_INERTIAL:
      "Le point satellite contredit la position que la solution inertielle attribue à l’avion. L’inertiel dérive, mais il dérive régulièrement et ne peut pas être falsifié de l’extérieur : sur une courte fenêtre, c’est un témoin utile contre une source qui a sauté.",
    SOLUTION_SEPARATION:
      "La solution toutes sources s’est écartée d’un sous-filtre qui ne reçoit jamais de données satellite, de plus que leur incertitude combinée ne l’autorise. C’est le test conçu pour la dérive lente : le sous-filtre ne peut pas suivre le mensonge, donc l’écart croît avec l’erreur injectée au lieu d’être absorbé dans l’état. Il n’apporte de pouvoir de détection que là où existe une référence absolue indépendante — retirez le capteur de vision et il retombe sur la barrière ordinaire.",
    MEASUREMENT_STALE:
      "L’échantillon est plus ancien que la limite de fraîcheur. Rien n’est faux dans la valeur : elle était vraie au moment où elle a été prise. C’est pour cela que l’instant d’échantillonnage et l’instant de livraison sont transportés séparément dans tout le bus de mesures.",
    SEQUENCE_REPEATED:
      "La source republie un échantillon qu’elle a déjà envoyé. Un capteur figé produit des nombres parfaitement plausibles indéfiniment : c’est le numéro de séquence qui l’attrape, et non un test sur la valeur.",
    SOURCE_UNAVAILABLE:
      "La source a cessé de délivrer des mesures utilisables. C’est une observation, pas une décision — rien n’a été exclu, il n’arrive simplement plus rien — et c’est pour cette raison que c’est rapporté différemment.",
    SOURCE_RETURNED:
      "La source délivre à nouveau. Lui refaire confiance est une décision distincte, régie par la fenêtre de récupération.",
    NIS_NORMAL_CLEARED:
      "Les résidus sont normaux depuis assez longtemps pour refaire confiance à la source. L’hystérésis est délibérée : lever la suspicion à la première bonne mesure laisserait une source entrer et sortir de la solution par intermittence.",
    RECOVERY_WINDOW_ELAPSED:
      "La fenêtre de récupération est écoulée et la source est de nouveau cohérente. Sa durée est un paramètre réglé et un vrai compromis : c’est du temps passé à voler sans une source peut-être déjà saine.",
    QUALITY_BELOW_THRESHOLD:
      "La source a déclaré sa propre qualité trop faible pour être utilisée. Croire cette déclaration coûte moins cher, et est plus honnête, que de déduire la même chose des résidus plusieurs secondes plus tard.",
    VELOCITY_INCONSISTENT:
      "La vitesse rapportée ne concorde pas avec le reste de la solution alors même que la position semble plausible. Les seuls résidus de position ne savent pas exprimer cela — d’où un code de raison qui lui est propre.",
    REDUNDANCY_INSUFFICIENT:
      "Il ne reste pas assez de sources indépendantes pour soutenir une revendication d’intégrité. Le comportement correct à ce stade est de le dire — déclarer une faible confiance — plutôt que de continuer à publier une position assortie d’un niveau de protection que plus rien ne soutient.",
    INNOVATION_COVARIANCE_INVALID:
      "La covariance d’innovation n’était pas exploitable : la mise à jour a été refusée plutôt qu’appliquée à l’aveugle. Refuser est le sens prudent — une mauvaise mise à jour est plus difficile à rattraper qu’une mise à jour manquante.",
    MANUAL_ISOLATION: "Isolée sur demande d’un opérateur, et non par un test.",
    NONE: "Aucun code de raison : la source est nominale.",
  },
};

export const NARRATION: Record<Lang, Narration> = { fr, en };
