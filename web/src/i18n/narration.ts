// AEROLAB RESILIENCE - live commentary for the Live Lab.
//
// The event log is written for a reader who already knows what a Normalized
// Innovation Squared is and why a source gets isolated. That reader is not the
// only one who opens this page, and even they benefit from being told which
// moment of the scenario they are looking at.
//
// This module is the plain-language track that runs alongside: what is
// happening right now, why this is the interesting moment, and what to watch.
// It states no number the engine did not produce — every value passed in comes
// from the frame or the scenario file.
import type { Lang } from "./strings";

export interface Beat {
  headline: string;
  text: string;
  watch: string;
}

export interface Narration {
  title: string;
  objectiveLabel: string;
  watchLabel: string;

  waiting: Beat;
  /** A scenario that injects nothing: the false-alert control case. */
  nominal: Beat;
  /** Before the injection, counting down. */
  beforeFault: (seconds: string, fault: string, target: string) => Beat;
  /** Injected, and nothing has reacted yet. */
  undetected: (fault: string, target: string, seconds: string) => Beat;
  /** At least one architecture has flagged the faulted source. */
  detected: (architecture: string, seconds: string, target: string) => Beat;
  /** The faulted source has been excluded from a solution. */
  isolated: (architecture: string, target: string, seconds: string) => Beat;
  /** The injection window has closed. */
  over: (bestName: string, best: string, worstName: string, worst: string) => Beat;
  /** The run has reached its end. */
  finished: (bestName: string, best: string, worstName: string, worst: string) => Beat;
}

const en: Narration = {
  title: "What is happening",
  objectiveLabel: "This scenario",
  watchLabel: "Watch",

  waiting: {
    headline: "Starting up",
    text: "The filters are converging on their first solution. Nothing is being injected yet.",
    watch: "The error settling to a couple of metres. That is the baseline everything after is judged against.",
  },

  nominal: {
    headline: "Nominal flight — nothing is being broken",
    text: "This scenario injects no fault at all. Every sensor behaves. It exists to measure how often the integrity architectures raise an alarm when there is nothing to raise one about.",
    watch: "The integrity log staying empty. Anything that appears in it is a false alert, and a false alert is a real cost: it throws away a source that was working.",
  },

  beforeFault: (seconds, fault, target) => ({
    headline: `Nominal — ${seconds} s before the injection`,
    text: `Every source agrees and every architecture is within a few metres of the truth. In ${seconds} seconds, ${fault.toLowerCase()} will be applied to the ${target} measurements, and to nothing else.`,
    watch: "How close the five architectures are to each other right now. They are about to stop agreeing.",
  }),

  undetected: (fault, target, seconds) => ({
    headline: "Fault injected — nobody has reacted yet",
    text: `${fault} has been applied to the ${target} measurements for ${seconds} s. The trajectory has not changed: only what the aircraft is being told about it. No architecture has flagged anything.`,
    watch: "The gap opening between the architectures on the chart, and the NIS of the faulted source climbing towards its gate.",
  }),

  detected: (architecture, seconds, target) => ({
    headline: "Detected",
    text: `${architecture} was the first to flag ${target}, ${seconds} s after the injection. That delay is the metric: an architecture that never notices scores no detection at all, which is reported as a dash rather than as zero.`,
    watch: "Whether it isolates the source or waits. Isolating too early on a transient throws away a good sensor; waiting too long lets the error grow.",
  }),

  isolated: (architecture, target, seconds) => ({
    headline: "Source isolated",
    text: `${architecture} noticed ${target} ${seconds} s after the injection and has now excluded it, continuing on the sources it still trusts. That is a decision with a reason attached, and both are in the integrity log.`,
    watch: "The isolated architectures pulling back towards the truth while the ones that trusted the faulted source follow it away.",
  }),

  over: (bestName, best, worstName, worst) => ({
    headline: "Injection over — the difference is the result",
    text: `The faulted source is behaving again. Right now ${bestName} is ${best} m from the truth and ${worstName} is ${worst} m. Both saw exactly the same measurements, so the difference is attributable to the architecture and not to luck.`,
    watch: "How long the architectures that isolated the source take to trust it again. That recovery hysteresis is deliberate, and it is tuned.",
  }),

  finished: (bestName, best, worstName, worst) => ({
    headline: "Run complete",
    text: `Final spread: ${bestName} at ${best} m against the truth, ${worstName} at ${worst} m. The report page has the metrics, the acceptance verdict and the provenance — commit, compiler, and the hash of the scenario that produced them.`,
    watch: "The report. And then run it again on a different seed: one run is an anecdote, the published figures are a thousand of them.",
  }),
};

const fr: Narration = {
  title: "Ce qui se passe",
  objectiveLabel: "Ce scénario",
  watchLabel: "À surveiller",

  waiting: {
    headline: "Démarrage",
    text: "Les filtres convergent vers leur première solution. Rien n’est encore injecté.",
    watch: "L’erreur qui se stabilise autour de quelques mètres. C’est la référence à laquelle tout le reste sera comparé.",
  },

  nominal: {
    headline: "Vol nominal — on ne casse rien",
    text: "Ce scénario n’injecte aucune faute. Tous les capteurs se comportent normalement. Il sert à mesurer à quelle fréquence les architectures d’intégrité déclenchent une alarme alors qu’il n’y a rien à signaler.",
    watch: "Le journal d’intégrité, qui doit rester vide. Tout ce qui s’y affiche est une fausse alerte — et une fausse alerte coûte cher : elle jette une source qui fonctionnait.",
  },

  beforeFault: (seconds, fault, target) => ({
    headline: `Nominal — injection dans ${seconds} s`,
    text: `Toutes les sources concordent et les cinq architectures sont à quelques mètres de la vérité. Dans ${seconds} secondes, ${fault.toLowerCase()} sera appliqué aux mesures de la source ${target}, et à rien d’autre.`,
    watch: "À quel point les cinq architectures sont proches en ce moment. Elles vont cesser d’être d’accord.",
  }),

  undetected: (fault, target, seconds) => ({
    headline: "Faute injectée — personne n’a encore réagi",
    text: `${fault} s’applique aux mesures de la source ${target} depuis ${seconds} s. La trajectoire, elle, n’a pas bougé : seul ce qu’on en raconte à l’avion a changé. Aucune architecture n’a signalé quoi que ce soit.`,
    watch: "L’écart qui se creuse entre les architectures sur le graphique, et le NIS de la source fautée qui monte vers son seuil.",
  }),

  detected: (architecture, seconds, target) => ({
    headline: "Détecté",
    text: `${architecture} a été la première à signaler ${target}, ${seconds} s après l’injection. Ce délai est la métrique : une architecture qui ne remarque jamais rien n’obtient aucune détection, ce qui est rapporté par un tiret et non par un zéro.`,
    watch: "Si elle isole la source ou si elle attend. Isoler trop tôt sur un transitoire jette un capteur sain ; attendre trop laisse l’erreur grandir.",
  }),

  isolated: (architecture, target, seconds) => ({
    headline: "Source isolée",
    text: `${architecture} a repéré ${target} ${seconds} s après l’injection et vient de l’exclure ; elle continue sur les sources auxquelles elle fait encore confiance. C’est une décision, elle a une raison, et les deux figurent dans le journal d’intégrité.`,
    watch: "Les architectures qui ont isolé revenir vers la vérité, pendant que celles qui ont fait confiance à la source fautée la suivent au loin.",
  }),

  over: (bestName, best, worstName, worst) => ({
    headline: "Injection terminée — l’écart est le résultat",
    text: `La source fautée se comporte à nouveau normalement. À cet instant, ${bestName} est à ${best} m de la vérité et ${worstName} à ${worst} m. Les deux ont vu exactement les mêmes mesures : l’écart est donc imputable à l’architecture, pas à la chance.`,
    watch: "Le temps que mettent les architectures ayant isolé la source à lui refaire confiance. Cette hystérésis de récupération est délibérée, et elle est réglée.",
  }),

  finished: (bestName, best, worstName, worst) => ({
    headline: "Essai terminé",
    text: `Écart final : ${bestName} à ${best} m de la vérité, ${worstName} à ${worst} m. La page Rapport contient les métriques, le verdict d’acceptation et la provenance — commit, compilateur, et l’empreinte du scénario qui les a produits.`,
    watch: "Le rapport. Puis relancez sur une autre graine : un essai est une anecdote, les chiffres publiés en agrègent mille.",
  }),
};

export const NARRATION: Record<Lang, Narration> = { fr, en };
