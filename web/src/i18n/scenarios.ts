// AEROLAB RESILIENCE - scenario catalogue text, French.
//
// The scenario FILES stay in technical English: they are the artefact the
// campaign reads, they carry the requirement identifiers, and NFR-018 keeps the
// repository in one language so that a reader comparing the screen against a
// file sees the same words. But the Web Lab is bilingual, and leaving fourteen
// English descriptions in the middle of a French interface is exactly the
// half-finished translation this project was told off for.
//
// So: the French here, the English from the file. Anything without an entry
// falls back to the file, which is the correct failure mode for a scenario
// added later.
//
// The requirement tags in parentheses (AT-004, INT-016 ...) are identifiers and
// are never translated.
export interface ScenarioText {
  name: string;
  description: string;
  objective: string;
}

const fr: Record<string, ScenarioText> = {
  "SCN-001": {
    name: "Approche nominale",
    description:
      "Aucune faute. Établit la précision de référence et, surtout, le budget de fausses alertes auquel tous les autres scénarios sont comparés.",
    objective: "Précision de référence et taux de fausse alerte (AT-003, INT-020).",
  },
  "SCN-002": {
    name: "Perte totale du GNSS",
    description:
      "Le GNSS devient indisponible à t = 30 s et ne revient jamais. Éprouve la continuité sans aucune source satellite absolue.",
    objective: "Continuité sous perte totale du GNSS.",
  },
  "SCN-003": {
    name: "Falsification GNSS en échelon",
    description:
      "Un décalage instantané de 100 m est ajouté à la position GNSS synthétique à t = 30 s. Le cas franc pour le rejet sur innovation.",
    objective: "Détection et isolation d’une incohérence brutale et de grande amplitude (AT-004).",
  },
  "SCN-004": {
    name: "Dérive lente du GNSS",
    description:
      "La position GNSS synthétique dérive en rampe de 0 à 150 m vers l’est en 45 s, à partir de t = 25 s. Le cas adverse : le filtre suit la rampe, l’innovation reste petite, et un test du khi-deux sur l’innovation perd son pouvoir.",
    objective:
      "Mesurer le plancher de détectabilité du rejet sur innovation face à la séparation de solutions. Aucun résultat n’est présumé : une non-détection ici est un résultat publié, pas un bug (section 8.1).",
  },
  "SCN-005": {
    name: "Vitesse GNSS incohérente",
    description:
      "La position reste plausible tandis que la vitesse rapportée est décalée d’environ 12 m/s. Éprouve la cohérence entre canaux plutôt que l’amplitude du résidu de position.",
    objective:
      "Un code de raison dédié pour une incohérence que le seul rejet sur la position ne sait pas nommer (INT-016).",
  },
  "SCN-006": {
    name: "Échelon de biais inertiel",
    description:
      "Un échelon de 0,35 m/s² sur l’accéléromètre x du repère avion et de 0,01 rad/s sur le gyromètre z, à t = 25 s.",
    objective: "Dégradation inertielle avec et sans référence absolue externe.",
  },
  "SCN-007": {
    name: "Marche aléatoire inertielle sévère",
    description:
      "Une bouffée de bruit soutenue sur le canal inertiel, très au-delà de la densité spectrale modélisée.",
    objective:
      "Cohérence du filtre face à un niveau de bruit non modélisé : la covariance reflète-t-elle la dégradation, ou le filtre reste-t-il trop confiant ?",
  },
  "SCN-008": {
    name: "Capteur figé",
    description:
      "Le GNSS répète sa dernière valeur et son dernier horodatage à partir de t = 30 s. La valeur reste parfaitement plausible ; seul l’âge la trahit.",
    objective:
      "Détection de péremption par l’âge de la mesure et par la répétition de séquence (INT-018, INT-019).",
  },
  "SCN-009": {
    name: "Bouffée de latence",
    description:
      "1,4 s de retard de livraison supplémentaire sur le canal GNSS pendant 30 s. Les horodatages d’échantillonnage sont intacts, donc l’âge réel reste observable.",
    objective:
      "Traitement des mesures retardées : retraitées par rollback dans la fenêtre du tampon, refusées comme périmées au-delà (NAV-009).",
  },
  "SCN-010": {
    name: "Vision dégradée",
    description:
      "La qualité vision décroît de 0,9 à 0,05 en 40 s. La source continue de publier tout du long : elle se dégrade, elle ne disparaît pas.",
    objective: "Dégradation progressive et rapport de confiance honnête (SENS-018).",
  },
  "SCN-011": {
    name: "Perte puis retour du GNSS",
    description: "Le GNSS est indisponible de t = 25 s à t = 55 s, puis revient nominal.",
    objective: "Réintégration via une fenêtre de récupération, sans saut de position non borné (INT-007).",
  },
  "SCN-012": {
    name: "Double faute",
    description:
      "Une falsification GNSS en échelon et un biais accélérométrique, tous deux à partir de t = 30 s. La redondance nécessaire pour arbitrer entre les deux a disparu.",
    objective:
      "Comportement à la limite de l’architecture. La bonne réponse est de déclarer une faible confiance, pas de forcer une solution (INT-013, INT-014).",
  },
  "SCN-013": {
    name: "Valeur aberrante de pseudodistance",
    description:
      "Un biais de 60 m sur la distance d’un satellite d’une constellation synthétique de huit.",
    objective:
      "Détection et exclusion de type RAIM là où la redondance le permet (INT-021). Aucune conformité à une norme RAIM n’est revendiquée.",
  },
  "SCN-014": {
    name: "Régression par rejeu",
    description:
      "Un vol nominal sur le profil d’approche avec virage, servant de référence au rejeu déterministe et au contrôle de parité natif / WebAssembly.",
    objective: "Reproductibilité (AT-001, AT-009, AT-014).",
  },
};

/**
 * French text for a scenario, or `null` when there is none and the caller
 * should use what the scenario file says.
 */
export function scenarioText(lang: string, id: string): ScenarioText | null {
  if (lang !== "fr") return null;
  return fr[id] ?? null;
}
