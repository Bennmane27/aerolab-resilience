// AEROLAB RESILIENCE - user interface strings, French and English.
//
// NFR-018 keeps the repository documentation in technical English; the Web Lab
// is the public face of the project and is bilingual, because the audience it
// was built for reads French and the disclaimers in particular have to be
// understood rather than skimmed.
//
// Rule for translators: the reason codes, sensor states and navigation modes
// that come out of the C++ engine are NEVER translated. They are the identifiers
// that appear in the telemetry, in the manifests and in the requirement
// catalogue, and a reader comparing the screen against a JSON file has to see
// the same token. Only the explanation beside them is translated.
//
// Second rule, learned the hard way: translate the CONCEPT, not the word. The
// first pass of the French left "run", "build" and "gating" in place because
// they are what the English says, which reads as a half-finished translation to
// anyone actually using the page. The French terms of art are essai, compilation
// and rejet sur innovation, and they are what this file uses now. Where an
// English term genuinely is the French one used in the field — GNSS, NIS, EKF,
// commit, spoofing — it stays.

export type Lang = "fr" | "en";

export interface Strings {
  // --- shell ---------------------------------------------------------------
  tagline: string;
  disclaimer: string;
  nav: {
    landing: string;
    scenarios: string;
    lab: string;
    compare: string;
    engineering: string;
    report: string;
    methodology: string;
    failures: string;
  };
  langLabel: string;

  // --- landing -------------------------------------------------------------
  landing: {
    title: string;
    quote: string;
    lead1: string;
    lead2: string;
    start: string;
    loading: string;
    source: string;
    audience: {
      publicLabel: string;
      publicText: string;
      recruiterLabel: string;
      recruiterText: string;
      engineerLabel: string;
      engineerText: string;
    };
  };

  // --- scenarios -----------------------------------------------------------
  scenarios: {
    title: string;
    lead: string;
    loading: string;
    faultCount: (n: number) => string;
    nominal: string;
    duration: string;
  };

  // --- live lab ------------------------------------------------------------
  lab: {
    chooseFirst: string;
    truthBadge: string;
    /** Short form for the legend row; must NOT repeat truthBadge verbatim. */
    truthShort: string;
    truthNotAvailable: string;
    seed: string;
    seedHint: string;
    transport: string;
    pause: string;
    resume: string;
    restart: string;
    speed: string;
    scrub: string;
    scrubHint: string;
    scrubFrame: (i: number, n: number) => string;
    camera: string;
    cameraChase: string;
    cameraMap: string;
    cameraRunway: string;
    cameraFree: string;
    cameraHint: string;
    cameraModeHint: string;
    sensorHealth: string;
    solutions: string;
    errorChart: string;
    events: string;
    noEvents: string;
    show: string;
    phase: string;
    altitude: string;
    groundSpeed: string;
    roll: string;
    pitch: string;
    distanceToThreshold: string;
    belowGround: string;
    belowGroundHelp: string;
    resetView: string;
    followAircraft: string;
    showLegend: string;
    expand: string;
    collapse: string;
    expandHint: string;
    collapseHint: string;
    legend: string;
    legendScale: string;
  };

  // --- event log -----------------------------------------------------------
  // The engine's fault type and target identifiers stay as they are; the
  // sentence beside them names the fault in words.
  eventLog: {
    faultArmed: string;
    faultEnded: string;
    faultArmedWhy: (type: string, target: string) => string;
    faultEndedWhy: (type: string, target: string) => string;
    faultTypes: Record<string, string>;
    sensorNames: Record<string, string>;
  };

  // --- shared panels -------------------------------------------------------
  panels: {
    architecture: string;
    error: string;
    sigmaH: string;
    mode: string;
    age: string;
    quality: string;
    threshold: string;
    noSolution: string;
    waiting: string;
    sensorNames: Record<string, string>;
  };

  // --- charts --------------------------------------------------------------
  chart: {
    time: string;
    positionError: string;
    nis: string;
    sigmaHorizontal: string;
    faultActive: string;
    noData: string;
    hoverHint: string;
    pinned: string;
    clickToPin: string;
    clickToUnpin: string;
    logScale: string;
    linearScale: string;
  };

  // --- compare -------------------------------------------------------------
  compare: {
    title: string;
    lead: string;
    chartTitle: string;
    metricsTitle: string;
    rmse: string;
    p95: string;
    max: string;
    modeNow: string;
    caveat: string;
  };

  // --- engineering ---------------------------------------------------------
  engineering: {
    title: string;
    lead: string;
    nisTitle: string;
    sigmaTitle: string;
    sigmaCaveat: string;
    raimTitle: string;
    raimStatistic: string;
    raimThreshold: string;
    raimDetected: string;
    raimExcluded: string;
    raimNone: string;
    raimCaveat: string;
    yes: string;
    no: string;
  };

  // --- report --------------------------------------------------------------
  report: {
    title: string;
    running: string;
    none: string;
    provenance: string;
    scenario: string;
    seed: string;
    commit: string;
    compiler: string;
    scenarioHash: string;
    configHash: string;
    verdict: string;
    failures: string;
    metrics: string;
    ttd: string;
    tti: string;
    availability: string;
    nisPerDof: string;
    dashHelp: string;
    manifest: string;
    manifestHelp: string;
    download: string;
  };

  // --- modes and reasons ---------------------------------------------------
  modeHelp: Record<string, string>;
  reasonHelp: Record<string, string>;
  stateHelp: Record<string, string>;

  // --- methodology ---------------------------------------------------------
  methodology: {
    title: string;
    blockquote: string;
    h_experiment: string;
    p_experiment1: string;
    p_experiment2: string;
    h_safety: string;
    p_safety: string;
    h_arch: string;
    arch: Array<{ name: string; text: string }>;
    h_why: string;
    p_why1: string;
    p_why2: string;
    h_assumptions: string;
    assumptions: string[];
    h_repro: string;
    p_repro: string;
    h_industrial: string;
    p_industrial: string;
    h_build: string;
    buildVersion: string;
    buildCommit: string;
    buildType: string;
    buildCompiler: string;
    telemetrySchema: string;
    scenarioSchema: string;
    notLoaded: string;
    fullDocs: string;
  };

  // --- failure catalog -----------------------------------------------------
  failures: {
    title: string;
    lead: string;
    what: string;
    why: string;
    status: string;
    reproduce: string;
    entries: Array<{ id: string; title: string; what: string; why: string; status: string; reproduce: string }>;
  };

  errors: {
    coreLoad: string;
    catalogLoad: string;
    recovered: string;
    reload: string;
  };
}

const en: Strings = {
  tagline: "Break the navigation. Measure what survives.",
  disclaimer:
    'Simulation only. Every "attack" here is arithmetic applied to synthetic measurements inside this page — no radio, no signal, no receiver. Not a certified system, not affiliated with any manufacturer or authority.',
  nav: {
    landing: "Overview",
    scenarios: "Scenarios",
    lab: "Live Lab",
    compare: "Compare",
    engineering: "Engineering",
    report: "Report",
    methodology: "Methodology",
    failures: "Failure catalog",
  },
  langLabel: "Language",

  landing: {
    title: "Can you make an aircraft lose its position?",
    quote: "“Break the navigation. Measure what survives.”",
    lead1:
      "This page breaks the navigation sensors of a simulated aircraft on purpose — the satellite fix disappears, or freezes, or quietly starts lying — and then measures which strategies still know where the aircraft is, how fast they notice, and how often they cry wolf.",
    lead2:
      "The engine running below is the same C++ core the offline benchmark uses, compiled to WebAssembly. Nothing is faked for the demo: every number on screen comes out of the same filters and the same integrity logic that produce the published results.",
    start: "Choose a scenario",
    loading: "Loading the core…",
    source: "Source and benchmark report",
    audience: {
      publicLabel: "If you are not an engineer",
      publicText:
        "An aircraft works out where it is by combining several instruments. Some of them can be fooled. This lab breaks one on purpose and shows you which combinations survive.",
      recruiterLabel: "If you are a recruiter",
      recruiterText:
        "C++17 core compiled native and to WebAssembly, deterministic simulation, controlled fault injection, error-state Kalman filtering, integrity monitoring, solution separation, Monte Carlo benchmarking, requirement traceability and a published failure catalog.",
      engineerLabel: "If you work in navigation",
      engineerText:
        "Closed-form truth, per-sensor noise models with sample and delivery timestamps, a delivery-ordered measurement bus with rollback reprocessing, a 15-state error-state EKF, NIS gating with persistence and recovery hysteresis, chi-square solution separation against a GNSS-free sub-filter, and a snapshot RAIM-like residual test.",
    },
  },

  scenarios: {
    title: "Scenario catalogue",
    lead:
      "Fourteen scenarios, each a versioned file with its own machine-readable acceptance block. The same files drive the Monte Carlo campaign; this page reads them directly rather than a copy, so what you run here is what the benchmark ran.",
    loading: "Loading the scenario catalogue…",
    faultCount: (n) => (n === 1 ? "1 injected fault" : `${n} injected faults`),
    nominal: "no fault",
    duration: "duration",
  },

  lab: {
    chooseFirst: "Choose a scenario first.",
    truthBadge: "SIMULATION TRUTH",
    truthShort: "Truth",
    truthNotAvailable: "not available to any estimator",
    seed: "seed",
    seedHint: "press Enter to re-run",
    transport: "Transport",
    pause: "Pause",
    resume: "Resume",
    restart: "Restart",
    speed: "Speed",
    scrub: "Scrub through recorded frames",
    scrubHint: "Pause to scrub through the recorded run.",
    scrubFrame: (i, n) => `frame ${i} / ${n}`,
    camera: "Camera",
    cameraChase: "Chase",
    cameraMap: "Top",
    cameraRunway: "Runway",
    cameraFree: "Free",
    cameraHint: "Drag to orbit · scroll to zoom · right-drag to pan",
    cameraModeHint: "Looking around never changes the mode: chase stays chase.",
    sensorHealth: "Sensor health",
    solutions: "Solutions",
    errorChart: "Position error against simulation truth",
    events: "Integrity and fault events",
    noEvents: "No integrity event yet. A nominal run should stay empty.",
    show: "Show",
    phase: "phase",
    altitude: "altitude",
    groundSpeed: "ground speed",
    roll: "roll",
    pitch: "pitch",
    distanceToThreshold: "to threshold",
    belowGround: "below ground",
    belowGroundHelp:
      "This solution places the aircraft under the ground plane. That is physically impossible and is the clearest sign an estimate has come away from reality. Its marker becomes a hollow ring and is drawn straight through the surface, so the case stays visible instead of disappearing under the terrain.",
    resetView: "Reset view",
    followAircraft: "Follow aircraft",
    showLegend: "Legend",
    expand: "Full screen",
    collapse: "Exit full screen",
    expandHint: "Give the 3D view the whole window; the controls stay on the side.",
    collapseHint: "Back to the normal layout, with the chart and the event log.",
    legend: "Legend",
    legendScale:
      "Rings at 1 / 2 / 5 km from the threshold · arrow points north · ▼ means the estimate is below the ground.",
  },

  eventLog: {
    faultArmed: "FAULT ARMED",
    faultEnded: "FAULT ENDED",
    faultArmedWhy: (type, target) =>
      `${type} is now being applied to the ${target} measurements. The engine only ever transforms measurements — it never touches the trajectory.`,
    faultEndedWhy: (type, target) =>
      `${type}: over. The ${target} measurements are back to their nominal model.`,
    faultTypes: {
      gnss_position_step: "A sudden position offset",
      gnss_position_ramp: "A slow position drift",
      gnss_velocity_inconsistency: "A velocity inconsistent with the position",
      source_unavailable: "A total loss of the source",
      noise_burst: "A burst of excess noise",
      freeze: "A frozen, repeated measurement",
      latency: "An added delivery delay",
      imu_accel_bias: "An accelerometer bias",
      imu_gyro_bias: "A gyroscope bias",
      vision_degrade: "A degradation of the reported vision quality",
      pseudorange_outlier: "An outlier on one satellite pseudorange",
    },
    sensorNames: { gnss: "GNSS", imu: "IMU", baro: "Barometer", vision: "Vision" },
  },

  panels: {
    architecture: "Architecture",
    error: "Error",
    sigmaH: "σ horizontal",
    mode: "Mode",
    age: "age",
    quality: "quality",
    threshold: "threshold",
    noSolution: "No solution yet.",
    waiting: "Waiting for the first measurements.",
    sensorNames: { gnss: "GNSS", imu: "IMU", baro: "Barometer", vision: "Vision" },
  },

  chart: {
    time: "time (s)",
    positionError: "position error (m)",
    nis: "NIS (dimensionless)",
    sigmaHorizontal: "σ horizontal (m)",
    faultActive: "fault active",
    noData: "No data yet. Start a run to populate this plot.",
    hoverHint: "Move the pointer over the plot to read every value at that instant.",
    pinned: "pinned",
    clickToPin: "click to pin this instant",
    clickToUnpin: "click to release",
    logScale: "log",
    linearScale: "linear",
  },

  compare: {
    title: "Compare",
    lead:
      "Every architecture below saw the identical measurement sequence: the truth is generated once per tick and the same sensor stream is handed to all five. Any difference between these rows is attributable to the architecture, not to luck.",
    chartTitle: "Position error, all architectures, same data",
    metricsTitle: "Metrics so far (this run only)",
    rmse: "RMSE",
    p95: "P95",
    max: "Max",
    modeNow: "Mode now",
    caveat:
      "These are single-run numbers on one seed. The published figures are distributions over a thousand seeds per scenario; see the benchmark report in the repository.",
  },

  engineering: {
    title: "Engineering view",
    lead:
      "The statistics the integrity policy actually decides on. The Normalized Innovation Squared is the measured disagreement between a measurement and the filter prediction, normalised by the uncertainty the filter claims. Under a consistent filter it averages the number of degrees of freedom of the measurement; the dashed line is the gate.",
    nisTitle: "Normalized Innovation Squared per source, against the gate",
    sigmaTitle: "Filter uncertainty: reported horizontal sigma",
    sigmaCaveat:
      "A filter whose reported sigma stays small while its true error grows is overconfident. That gap, not the error alone, is what an integrity architecture has to catch.",
    raimTitle: "RAIM-like residual test (pseudorange scenarios only)",
    raimStatistic: "statistic",
    raimThreshold: "threshold",
    raimDetected: "detected",
    raimExcluded: "excluded satellite",
    raimNone: "none",
    raimCaveat:
      "Educational residual test. No conformance to any RAIM or ARAIM standard is claimed and no protection level is computed.",
    yes: "yes",
    no: "no",
  },

  report: {
    title: "Run report",
    running: "The run is still going. The report is produced when it finishes.",
    none: "No report yet — let a run finish.",
    provenance: "Provenance",
    scenario: "scenario",
    seed: "seed",
    commit: "commit",
    compiler: "compiler",
    scenarioHash: "scenario hash",
    configHash: "config hash",
    verdict: "verdict",
    failures: "Acceptance failures",
    metrics: "Metrics",
    ttd: "TTD",
    tti: "TTI",
    availability: "Avail",
    nisPerDof: "NIS/dof",
    dashHelp:
      "A dash in time-to-detect means no detection was raised. It is deliberately not shown as zero: averaging a missed detection as an instant response is the easiest way to publish a flattering benchmark.",
    manifest: "Full manifest",
    manifestHelp:
      "This is the same JSON the native command line writes next to a run. Download it to keep a record of exactly what produced the numbers above.",
    download: "Download the manifest",
  },

  modeHelp: {
    INITIALIZING: "The filter has not yet converged on a solution.",
    NORMAL: "Every source is consistent and in use.",
    DEGRADED:
      "At least one source has been isolated or is unavailable; the solution continues on the rest.",
    DEAD_RECKONING:
      "No absolute position source is in use; the solution is coasting on inertial data alone.",
    LOW_CONFIDENCE:
      "Too little redundancy remains to support an integrity claim. The position is still published but must not be trusted.",
    UNSAFE: "The policy criteria are no longer met. The solution is not usable.",
  },
  stateHelp: {
    ACTIVE: "In use, and consistent with everything else.",
    SUSPECT: "Disagreeing, but not yet enough or not for long enough to exclude it.",
    ISOLATED: "Excluded by the integrity policy. This is a decision, and it has a reason.",
    UNAVAILABLE: "Not delivering usable measurements. This is an observation, not a decision.",
  },
  reasonHelp: {
    NONE: "Nominal.",
    NIS_ABOVE_THRESHOLD:
      "The measurement disagrees with the filter prediction by more than the gate allows for this source.",
    NIS_PERSISTENT: "The disagreement persisted long enough to rule out a single outlier.",
    NIS_NORMAL_CLEARED: "Residuals have been normal long enough for the source to be trusted again.",
    RECOVERY_WINDOW_ELAPSED: "The recovery window has elapsed and the source is consistent again.",
    MEASUREMENT_STALE:
      "The sample timestamp is older than the freshness limit: the value may look plausible but it is not current.",
    SEQUENCE_REPEATED: "The source is repeating a previous sample rather than producing a new one.",
    SOURCE_UNAVAILABLE: "The source stopped delivering usable measurements.",
    SOURCE_RETURNED: "The source is delivering again.",
    CROSS_CHECK_INERTIAL:
      "GNSS disagrees with where the inertial solution says the aircraft should be.",
    CROSS_CHECK_VISION: "GNSS and the runway-relative vision fix disagree about the position.",
    SOLUTION_SEPARATION:
      "The all-sources solution has drifted away from the GNSS-free sub-filter by more than their combined uncertainty allows.",
    INNOVATION_COVARIANCE_INVALID:
      "The innovation covariance was not usable; the update was refused.",
    VELOCITY_INCONSISTENT:
      "The reported velocity is not consistent with the rest of the solution, even though the position looks plausible.",
    QUALITY_BELOW_THRESHOLD: "The source reports a quality too low for its measurement to be usable.",
    REDUNDANCY_INSUFFICIENT: "Not enough independent sources remain to support an integrity claim.",
    MANUAL_ISOLATION: "Isolated by an operator request.",
  },

  methodology: {
    title: "Methodology",
    blockquote:
      "This is a research and engineering demonstrator for evaluating navigation integrity and resilience. It is not a certified system, it makes no claim of conformance to DO-178C, RAIM, ARAIM or any other standard, and it is not affiliated with or endorsed by any aircraft manufacturer, equipment supplier or aviation authority.",
    h_experiment: "What the experiment is",
    p_experiment1:
      "A deterministic trajectory is generated in closed form. Synthetic sensors read that trajectory and add their own noise, bias and latency. A fault engine then applies arithmetic transformations to those measurements — and only to those measurements. Five navigation architectures consume the identical resulting stream, and their outputs are compared against the trajectory that produced it.",
    p_experiment2:
      "The fault engine is never given access to the trajectory. That is enforced by the type of the function, not by convention: it takes an array of measurements and nothing else. It is the property that makes every number here meaningful, so it also has its own acceptance test.",
    h_safety: "Safety boundary",
    p_safety:
      'Everything called a spoof, a jam or an attack in this project is a number added to an array inside one process. There is no radio, no signal generator, no receiver model, no transmission parameter and no operational procedure for interfering with a real system anywhere in the source. The word "spoofing" here means: add an offset to a synthetic position that never left this program.',
    h_arch: "The five architectures",
    arch: [
      {
        name: "GNSS only",
        text: "Position is the last valid satellite fix. It exists to show what happens when a single absolute source is trusted without question.",
      },
      {
        name: "INS dead reckoning",
        text: "Strapdown inertial integration with no absolute update ever. It bounds how long a solution can coast.",
      },
      {
        name: "EKF",
        text: "A 15-state error-state Kalman filter fusing inertial, satellite, barometric and runway-relative vision measurements, with no integrity layer. The control case.",
      },
      {
        name: "EKF + innovation gating",
        text: "The same filter, plus a chi-square test on each measurement's innovation, with a persistence counter before a source is suspected, an isolation delay, and a recovery window before it is trusted again.",
      },
      {
        name: "EKF + solution separation",
        text: "The same filter and the same gate, plus a second, independent test: the all-sources solution is compared against a sub-filter that never receives satellite data at all.",
      },
    ],
    h_why: "Why solution separation exists here, and what it actually bought",
    p_why1:
      "A chi-square gate on the innovation is structurally weak against a slow drift. As a spoofed position ramps away, the filter state follows it, so the innovation — the disagreement between the measurement and the prediction — stays small and the gate never fires. A sub-filter that does not receive the drifting source cannot follow it, so the separation between the two solutions grows with the injected error instead of being absorbed by it.",
    p_why2:
      "Measured, that reasoning turned out to be incomplete. In the default configuration the two policies are indistinguishable: a plain threshold on the distance between the satellite fix and the runway-relative vision fix was catching the drift first, and both architectures inherited its performance. Disable that cross check and they separate by a factor of six. Remove the vision sensor entirely and both collapse to identical numbers, because the satellite-free sub-filter then has no absolute reference and drifts faster than the spoof does. Solution separation converts an independent absolute reference into detection power; it does not create integrity out of nothing.",
    h_assumptions: "Assumptions and simplifications",
    assumptions: [
      "Flat earth, local north-east-down frame, no Coriolis or transport rate.",
      "Zero angle of attack: pitch equals flight path angle. A real aircraft holds a few degrees.",
      "No wind, therefore no crab angle.",
      "The barometric bias is not part of the state vector; it is absorbed as extra measurement uncertainty, which is honest but suboptimal.",
      "The vision sensor is a synthetic runway-relative measurement, not computer vision on rendered images.",
      "The satellite constellation used by the residual test is a frozen geometry snapshot, not an orbital model.",
    ],
    h_repro: "Reproducibility",
    p_repro:
      "Every run is fully determined by a scenario file, a configuration file and a seed, and every run exports the content hash of both files, the commit, the compiler and the resulting metrics. Two runs of the same binary with the same inputs produce bit-identical output, and an acceptance test checks it. Bit-identical output across different compilers or across the native and WebAssembly builds is not claimed: floating-point library differences make it unattainable, so cross-build agreement is checked against a stated numeric tolerance and on the equivalence of the integrity events instead.",
    h_industrial: "Industrial context",
    p_industrial:
      "Satellite navigation interference has been the subject of published safety bulletins and coordinated action plans from European aviation authorities, and resilient positioning-navigation-timing is an active industrial programme area. Those publications establish that the problem is real and the methods are relevant. They say nothing about this implementation, and are cited in the repository with the date and the specific claim each one supports — never as an endorsement.",
    h_build: "Build",
    buildVersion: "core version",
    buildCommit: "commit",
    buildType: "build type",
    buildCompiler: "compiler",
    telemetrySchema: "telemetry schema",
    scenarioSchema: "scenario schema",
    notLoaded: "Core not loaded.",
    fullDocs: "Full requirement catalogue, traceability matrix, benchmark report and source:",
  },

  failures: {
    title: "Failure catalog",
    lead:
      "Every entry here is something that went wrong, or still goes wrong, in this project. They are published because a benchmark that only reports its successes is not a benchmark. Three of these were found by the platform catching its own simulator or its own metrics, which is the outcome a test bench is actually for.",
    what: "What happens.",
    why: "Why.",
    status: "Status.",
    reproduce: "Reproduce",
    entries: [],
  },

  errors: {
    coreLoad: "The WebAssembly core could not be loaded.",
    catalogLoad: "The scenario catalogue could not be loaded.",
    recovered:
      "Something went wrong in the interface. The simulation core is unaffected; the view has been reset.",
    reload: "Reset the view",
  },
};

const fr: Strings = {
  tagline: "Casser la navigation. Mesurer ce qui survit.",
  disclaimer:
    "Simulation uniquement. Chaque « attaque » ici est de l’arithmétique appliquée à des mesures synthétiques à l’intérieur de cette page — pas de radio, pas de signal, pas de récepteur. Système non certifié, sans affiliation à aucun constructeur ni à aucune autorité.",
  nav: {
    landing: "Présentation",
    scenarios: "Scénarios",
    lab: "Labo en direct",
    compare: "Comparer",
    engineering: "Ingénierie",
    report: "Rapport",
    methodology: "Méthodologie",
    failures: "Échecs documentés",
  },
  langLabel: "Langue",

  landing: {
    title: "Pouvez-vous faire perdre sa position à un avion ?",
    quote: "« Casser la navigation. Mesurer ce qui survit. »",
    lead1:
      "Cette page casse volontairement les capteurs de navigation d’un avion simulé — le point satellite disparaît, ou se fige, ou se met discrètement à mentir — puis mesure quelles stratégies savent encore où se trouve l’avion, en combien de temps elles s’en aperçoivent, et à quelle fréquence elles crient au loup.",
    lead2:
      "Le moteur qui tourne ci-dessous est le même cœur C++ que celui de la campagne de mesure hors ligne, compilé en WebAssembly. Rien n’est truqué pour la démonstration : chaque chiffre affiché sort des mêmes filtres et de la même logique d’intégrité que les résultats publiés.",
    start: "Choisir un scénario",
    loading: "Chargement du cœur…",
    source: "Code source et rapport de campagne",
    audience: {
      publicLabel: "Si vous n’êtes pas ingénieur",
      publicText:
        "Un avion détermine sa position en combinant plusieurs instruments. Certains peuvent être trompés. Ce laboratoire en casse un exprès et vous montre quelles combinaisons y survivent.",
      recruiterLabel: "Si vous recrutez",
      recruiterText:
        "Cœur C++17 compilé en natif et en WebAssembly, simulation déterministe, injection de fautes contrôlée, filtrage de Kalman à état d’erreur, surveillance d’intégrité, séparation de solutions, campagne Monte-Carlo, traçabilité des exigences et catalogue d’échecs publié.",
      engineerLabel: "Si vous travaillez en navigation",
      engineerText:
        "Vérité terrain en forme close, modèles de bruit par capteur avec horodatages d’échantillonnage et de livraison, bus de mesures ordonné par livraison avec retraitement par rollback, EKF à état d’erreur 15 états, rejet sur NIS avec persistance et hystérésis de récupération, séparation de solutions en khi-deux contre un sous-filtre sans GNSS, et test de résidus de type RAIM par instantané.",
    },
  },

  scenarios: {
    title: "Catalogue de scénarios",
    lead:
      "Quatorze scénarios, chacun un fichier versionné avec son propre bloc d’acceptation lisible par la machine. Ce sont ces mêmes fichiers qui pilotent la campagne Monte-Carlo ; cette page les lit directement plutôt qu’une copie, donc ce que vous lancez ici est exactement ce qui a produit les chiffres publiés.",
    loading: "Chargement du catalogue de scénarios…",
    faultCount: (n) => (n === 1 ? "1 faute injectée" : `${n} fautes injectées`),
    nominal: "aucune faute",
    duration: "durée",
  },

  lab: {
    chooseFirst: "Choisissez d’abord un scénario.",
    truthBadge: "VÉRITÉ DE SIMULATION",
    truthShort: "Vérité",
    truthNotAvailable: "inaccessible à tous les estimateurs",
    seed: "graine",
    seedHint: "Entrée pour relancer",
    transport: "Lecture",
    pause: "Pause",
    resume: "Reprendre",
    restart: "Relancer",
    speed: "Vitesse",
    scrub: "Parcourir les instants enregistrés",
    scrubHint: "Mettez en pause pour revenir en arrière dans l’essai enregistré.",
    scrubFrame: (i, n) => `pas ${i} / ${n}`,
    camera: "Caméra",
    cameraChase: "Poursuite",
    cameraMap: "Dessus",
    cameraRunway: "Piste",
    cameraFree: "Libre",
    cameraHint: "Glisser pour pivoter · molette pour zoomer · clic droit pour translater",
    cameraModeHint: "Regarder autour ne change jamais le mode : poursuite reste poursuite.",
    sensorHealth: "État des capteurs",
    solutions: "Solutions",
    errorChart: "Erreur de position par rapport à la vérité de simulation",
    events: "Intégrité et fautes injectées",
    noEvents:
      "Aucun événement d’intégrité pour l’instant. Sur un vol nominal, ce journal doit rester vide.",
    show: "Afficher",
    phase: "phase",
    altitude: "altitude",
    groundSpeed: "vitesse sol",
    roll: "roulis",
    pitch: "assiette",
    distanceToThreshold: "du seuil",
    belowGround: "sous la surface",
    belowGroundHelp:
      "Cette solution place l’avion sous le plan du sol. C’est physiquement impossible, et c’est le signe le plus net qu’elle a décroché de la réalité. Son marqueur devient un anneau creux, dessiné au travers de la surface, pour que ce cas reste visible au lieu de disparaître sous le décor.",
    resetView: "Recadrer la vue",
    followAircraft: "Suivre l’avion",
    showLegend: "Légende",
    expand: "Plein écran",
    collapse: "Quitter le plein écran",
    expandHint: "Donne toute la fenêtre à la vue 3D ; les commandes restent sur le côté.",
    collapseHint: "Revenir à la disposition normale, avec le graphique et le journal.",
    legend: "Légende",
    legendScale:
      "Anneaux à 1 / 2 / 5 km du seuil · la flèche indique le nord · ▼ signale une estimation sous le sol.",
  },

  eventLog: {
    faultArmed: "FAUTE ARMÉE",
    faultEnded: "FAUTE TERMINÉE",
    faultArmedWhy: (type, target) =>
      `${type} s’applique désormais aux mesures de la source ${target}. Le moteur ne transforme que des mesures : il ne touche jamais à la trajectoire.`,
    faultEndedWhy: (type, target) =>
      `${type} : c’est terminé. Les mesures de la source ${target} sont revenues à leur modèle nominal.`,
    faultTypes: {
      gnss_position_step: "Un saut brutal de position",
      gnss_position_ramp: "Une dérive lente de la position",
      gnss_velocity_inconsistency: "Une vitesse incohérente avec la position",
      source_unavailable: "Une perte totale de la source",
      noise_burst: "Une bouffée de bruit excédentaire",
      freeze: "Une mesure figée, répétée à l’identique",
      latency: "Un retard de livraison ajouté",
      imu_accel_bias: "Un biais sur les accéléromètres",
      imu_gyro_bias: "Un biais sur les gyromètres",
      vision_degrade: "Une dégradation de la qualité vision déclarée",
      pseudorange_outlier: "Une valeur aberrante sur une pseudodistance",
    },
    sensorNames: {
      gnss: "GNSS",
      imu: "centrale inertielle",
      baro: "baromètre",
      vision: "vision",
    },
  },

  panels: {
    architecture: "Architecture",
    error: "Erreur",
    sigmaH: "σ horizontal",
    mode: "Mode",
    age: "âge",
    quality: "qualité",
    threshold: "seuil",
    noSolution: "Aucune solution pour l’instant.",
    waiting: "En attente des premières mesures.",
    sensorNames: { gnss: "GNSS", imu: "Centrale inertielle", baro: "Baromètre", vision: "Vision" },
  },

  chart: {
    time: "temps (s)",
    positionError: "erreur de position (m)",
    nis: "NIS (sans dimension)",
    sigmaHorizontal: "σ horizontal (m)",
    faultActive: "faute en cours",
    noData: "Aucune donnée pour l’instant. Lancez un essai pour tracer la courbe.",
    hoverHint: "Déplacez le pointeur sur le graphique pour lire toutes les valeurs à cet instant.",
    pinned: "épinglé",
    clickToPin: "cliquer pour épingler cet instant",
    clickToUnpin: "cliquer pour détacher",
    logScale: "log",
    linearScale: "linéaire",
  },

  compare: {
    title: "Comparaison",
    lead:
      "Toutes les architectures ci-dessous ont vu exactement la même séquence de mesures : la vérité est générée une fois par pas de temps et le même flux capteur est remis aux cinq. Toute différence entre ces lignes est imputable à l’architecture, pas à la chance.",
    chartTitle: "Erreur de position, toutes architectures, mêmes données",
    metricsTitle: "Métriques à cet instant (cet essai uniquement)",
    rmse: "RMSE",
    p95: "P95",
    max: "Max",
    modeNow: "Mode actuel",
    caveat:
      "Ce sont les chiffres d’un seul essai, sur une seule graine. Les valeurs publiées sont des distributions sur mille graines par scénario ; voir le rapport de campagne dans le dépôt.",
  },

  engineering: {
    title: "Vue ingénierie",
    lead:
      "Les statistiques sur lesquelles la politique d’intégrité décide réellement. Le NIS (Normalized Innovation Squared) est le désaccord mesuré entre une mesure et la prédiction du filtre, normalisé par l’incertitude que le filtre revendique. Sous un filtre cohérent, il vaut en moyenne le nombre de degrés de liberté de la mesure ; la ligne pointillée est le seuil.",
    nisTitle: "NIS par source, comparé au seuil",
    sigmaTitle: "Incertitude du filtre : sigma horizontal déclaré",
    sigmaCaveat:
      "Un filtre dont le sigma déclaré reste petit alors que son erreur réelle grandit est trop confiant. C’est cet écart, et non l’erreur seule, qu’une architecture d’intégrité doit détecter.",
    raimTitle: "Test de résidus de type RAIM (scénarios à pseudodistances uniquement)",
    raimStatistic: "statistique",
    raimThreshold: "seuil",
    raimDetected: "détecté",
    raimExcluded: "satellite exclu",
    raimNone: "aucun",
    raimCaveat:
      "Test de résidus pédagogique. Aucune conformité à une norme RAIM ou ARAIM n’est revendiquée et aucun niveau de protection n’est calculé.",
    yes: "oui",
    no: "non",
  },

  report: {
    title: "Rapport d’essai",
    running: "L’essai est encore en cours. Le rapport est produit à son terme.",
    none: "Pas encore de rapport — laissez un essai aller à son terme.",
    provenance: "Provenance",
    scenario: "scénario",
    seed: "graine",
    commit: "commit",
    compiler: "compilateur",
    scenarioHash: "empreinte du scénario",
    configHash: "empreinte de la config",
    verdict: "verdict",
    failures: "Critères d’acceptation en échec",
    metrics: "Métriques",
    ttd: "TTD",
    tti: "TTI",
    availability: "Disponibilité",
    nisPerDof: "NIS/ddl",
    dashHelp:
      "Un tiret dans le temps de détection signifie qu’aucune détection n’a été levée. Ce n’est délibérément pas affiché comme zéro : moyenner une non-détection comme une réponse instantanée est la façon la plus simple de publier un résultat flatteur.",
    manifest: "Manifeste complet",
    manifestHelp:
      "C’est le même JSON que la ligne de commande native écrit à côté d’un essai. Téléchargez-le pour garder la trace exacte de ce qui a produit les chiffres ci-dessus.",
    download: "Télécharger le manifeste",
  },

  modeHelp: {
    INITIALIZING: "Le filtre n’a pas encore convergé vers une solution.",
    NORMAL: "Toutes les sources sont cohérentes et utilisées.",
    DEGRADED:
      "Au moins une source a été isolée ou est indisponible ; la solution continue sur les autres.",
    DEAD_RECKONING:
      "Aucune source de position absolue n’est utilisée ; la solution avance sur l’inertiel seul.",
    LOW_CONFIDENCE:
      "Il ne reste pas assez de redondance pour soutenir une revendication d’intégrité. La position continue d’être publiée, mais elle ne doit plus servir de référence.",
    UNSAFE: "Les critères de la politique ne sont plus satisfaits. La solution est inutilisable.",
  },
  stateHelp: {
    ACTIVE: "Utilisée, et cohérente avec tout le reste.",
    SUSPECT: "En désaccord, mais pas encore assez ni assez longtemps pour l’exclure.",
    ISOLATED: "Exclue par la politique d’intégrité. C’est une décision, et elle a une raison.",
    UNAVAILABLE: "Ne délivre plus de mesures utilisables. C’est une observation, pas une décision.",
  },
  reasonHelp: {
    NONE: "Nominal.",
    NIS_ABOVE_THRESHOLD:
      "La mesure s’écarte de la prédiction du filtre de plus que le seuil autorisé pour cette source.",
    NIS_PERSISTENT: "Le désaccord a persisté assez longtemps pour écarter l’hypothèse d’une valeur aberrante isolée.",
    NIS_NORMAL_CLEARED:
      "Les résidus sont normaux depuis assez longtemps pour refaire confiance à la source.",
    RECOVERY_WINDOW_ELAPSED:
      "La fenêtre de récupération est écoulée et la source est à nouveau cohérente.",
    MEASUREMENT_STALE:
      "L’horodatage d’échantillonnage est plus ancien que la limite de fraîcheur : la valeur peut sembler plausible mais elle n’est pas à jour.",
    SEQUENCE_REPEATED:
      "La source répète un échantillon précédent au lieu d’en produire un nouveau.",
    SOURCE_UNAVAILABLE: "La source a cessé de délivrer des mesures utilisables.",
    SOURCE_RETURNED: "La source délivre à nouveau.",
    CROSS_CHECK_INERTIAL:
      "Le GNSS est en désaccord avec la position que l’inertiel attribue à l’avion.",
    CROSS_CHECK_VISION:
      "Le GNSS et le point vision relatif à la piste sont en désaccord sur la position.",
    SOLUTION_SEPARATION:
      "La solution toutes sources s’est écartée du sous-filtre sans GNSS de plus que leur incertitude combinée ne l’autorise.",
    INNOVATION_COVARIANCE_INVALID:
      "La covariance d’innovation n’était pas exploitable ; la mise à jour a été refusée.",
    VELOCITY_INCONSISTENT:
      "La vitesse rapportée n’est pas cohérente avec le reste de la solution, alors même que la position semble plausible.",
    QUALITY_BELOW_THRESHOLD:
      "La source déclare une qualité trop faible pour que sa mesure soit exploitable.",
    REDUNDANCY_INSUFFICIENT:
      "Il ne reste pas assez de sources indépendantes pour soutenir une revendication d’intégrité.",
    MANUAL_ISOLATION: "Isolée sur demande d’un opérateur.",
  },

  methodology: {
    title: "Méthodologie",
    blockquote:
      "Ceci est un démonstrateur de recherche et d’ingénierie pour l’évaluation de l’intégrité et de la résilience de la navigation. Ce n’est pas un système certifié, il ne revendique aucune conformité à DO-178C, RAIM, ARAIM ou toute autre norme, et il n’est ni affilié ni approuvé par un avionneur, un équipementier ou une autorité de l’aviation.",
    h_experiment: "En quoi consiste l’expérience",
    p_experiment1:
      "Une trajectoire déterministe est générée en forme close. Des capteurs synthétiques lisent cette trajectoire et y ajoutent leur propre bruit, biais et latence. Un moteur de fautes applique ensuite des transformations arithmétiques à ces mesures — et uniquement à ces mesures. Cinq architectures de navigation consomment le flux identique qui en résulte, et leurs sorties sont comparées à la trajectoire qui l’a produit.",
    p_experiment2:
      "Le moteur de fautes n’a jamais accès à la trajectoire. Ce n’est pas une convention mais une contrainte du type de la fonction : elle prend un tableau de mesures et rien d’autre. C’est la propriété qui donne un sens à chaque chiffre présenté ici, elle a donc son propre test d’acceptation.",
    h_safety: "Frontière de sécurité",
    p_safety:
      "Tout ce qui est appelé spoofing, brouillage ou attaque dans ce projet est un nombre ajouté à un tableau à l’intérieur d’un seul processus. Il n’y a nulle part dans le code de radio, de générateur de signal, de modèle de récepteur, de paramètre d’émission ni de procédure opérationnelle permettant d’interférer avec un système réel. Le mot « spoofing » signifie ici : ajouter un décalage à une position synthétique qui n’a jamais quitté ce programme.",
    h_arch: "Les cinq architectures",
    arch: [
      {
        name: "GNSS seul",
        text: "La position est le dernier point satellite valide. Elle existe pour montrer ce qui arrive quand une source absolue unique est crue sans discussion.",
      },
      {
        name: "Navigation à l’estime",
        text: "Intégration inertielle sans jamais aucun recalage absolu. Elle mesure combien de temps une solution peut tenir sans le moindre recalage.",
      },
      {
        name: "EKF",
        text: "Un filtre de Kalman à état d’erreur à 15 états fusionnant inertiel, satellite, barométrique et vision relative à la piste, sans couche d’intégrité. Le cas témoin.",
      },
      {
        name: "EKF + rejet sur innovation",
        text: "Le même filtre, plus un test du khi-deux sur l’innovation de chaque mesure, avec un compteur de persistance avant qu’une source ne soit suspectée, un délai d’isolation, et une fenêtre de récupération avant qu’on lui refasse confiance.",
      },
      {
        name: "EKF + séparation de solutions",
        text: "Le même filtre et le même seuil, plus un second test indépendant : la solution toutes sources est comparée à un sous-filtre qui ne reçoit jamais aucune donnée satellite.",
      },
    ],
    h_why: "Pourquoi la séparation de solutions est là, et ce qu’elle a réellement apporté",
    p_why1:
      "Un test du khi-deux sur l’innovation est structurellement faible face à une dérive lente. À mesure que la position falsifiée s’éloigne en rampe, l’état du filtre la suit, donc l’innovation — l’écart entre la mesure et la prédiction — reste petite et le seuil n’est jamais franchi. Un sous-filtre qui ne reçoit pas la source dérivante ne peut pas la suivre : l’écart entre les deux solutions croît avec l’erreur injectée au lieu d’être absorbé par elle.",
    p_why2:
      "Mesuré, ce raisonnement s’est révélé incomplet. En configuration par défaut les deux politiques sont indiscernables : un simple seuil sur la distance entre le point satellite et le point vision relatif à la piste attrapait la dérive en premier, et les deux architectures héritaient de sa performance. Désactivez ce contrôle croisé et elles se séparent d’un facteur six. Retirez entièrement le capteur de vision et les deux s’effondrent à des chiffres identiques, car le sous-filtre sans satellite n’a alors plus de référence absolue et dérive plus vite que la falsification. La séparation de solutions convertit une référence absolue indépendante en pouvoir de détection ; elle ne fabrique pas de l’intégrité à partir de rien.",
    h_assumptions: "Hypothèses et simplifications",
    assumptions: [
      "Terre plane, repère local nord-est-bas, pas de Coriolis ni de vitesse de transport.",
      "Incidence nulle : l’assiette égale la pente de trajectoire. Un avion réel tient quelques degrés.",
      "Pas de vent, donc pas d’angle de dérive.",
      "Le biais barométrique ne fait pas partie du vecteur d’état ; il est absorbé comme incertitude de mesure supplémentaire, ce qui est honnête mais sous-optimal.",
      "Le capteur de vision est une mesure synthétique relative à la piste, pas de la vision par ordinateur sur des images rendues.",
      "La constellation utilisée par le test de résidus est un instantané de géométrie figé, pas un modèle orbital.",
    ],
    h_repro: "Reproductibilité",
    p_repro:
      "Chaque essai est entièrement déterminé par un fichier de scénario, un fichier de configuration et une graine, et chaque essai exporte l’empreinte de contenu des deux fichiers, le commit, le compilateur et les métriques obtenues. Deux exécutions du même binaire avec les mêmes entrées produisent une sortie identique au bit près, et un test d’acceptation le vérifie. L’identité au bit près entre compilateurs différents, ou entre la compilation native et la compilation WebAssembly, n’est pas revendiquée : les différences de bibliothèque mathématique la rendent inatteignable, l’accord entre compilations est donc vérifié contre une tolérance numérique déclarée et sur l’équivalence des événements d’intégrité.",
    h_industrial: "Contexte industriel",
    p_industrial:
      "Les interférences sur la navigation par satellite font l’objet de bulletins de sécurité publiés et de plans d’action coordonnés des autorités européennes de l’aviation, et le PNT résilient est un axe de programme industriel actif. Ces publications établissent que le problème est réel et que les méthodes sont pertinentes. Elles ne disent rien de cette implémentation, et sont citées dans le dépôt avec leur date et l’affirmation précise que chacune soutient — jamais comme une approbation.",
    h_build: "Compilation",
    buildVersion: "version du cœur",
    buildCommit: "commit",
    buildType: "type de compilation",
    buildCompiler: "compilateur",
    telemetrySchema: "schéma de télémétrie",
    scenarioSchema: "schéma de scénario",
    notLoaded: "Cœur non chargé.",
    fullDocs:
      "Catalogue complet des exigences, matrice de traçabilité, rapport de campagne et code source :",
  },

  failures: {
    title: "Échecs documentés",
    lead:
      "Chaque entrée ici est quelque chose qui a mal tourné, ou tourne encore mal, dans ce projet. Elles sont publiées parce qu’un benchmark qui ne rapporte que ses succès n’est pas un benchmark. Trois d’entre elles ont été trouvées par la plateforme elle-même, en prenant en défaut son propre simulateur ou ses propres métriques, ce qui est précisément la raison d’être d’un banc d’essai.",
    what: "Ce qui se passe.",
    why: "Pourquoi.",
    status: "Statut.",
    reproduce: "Reproduire",
    entries: [],
  },

  errors: {
    coreLoad: "Le cœur WebAssembly n’a pas pu être chargé.",
    catalogLoad: "Le catalogue de scénarios n’a pas pu être chargé.",
    recovered:
      "Un problème est survenu dans l’interface. Le cœur de simulation n’est pas affecté ; la vue a été réinitialisée.",
    reload: "Réinitialiser la vue",
  },
};

export const STRINGS: Record<Lang, Strings> = { fr, en };
