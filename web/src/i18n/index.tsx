// AEROLAB RESILIENCE - language context.
//
// The chosen language is persisted, and the document language attribute is kept
// in sync so that assistive technology announces the page correctly.
import { createContext, useCallback, useContext, useEffect, useMemo, useState } from "react";
import type { ReactNode } from "react";
import { STRINGS, type Lang, type Strings } from "./strings";
import { FAILURE_ENTRIES, type FailureEntry } from "./failures";
import { GUIDE, type GuideStrings } from "./guide";
import { NARRATION, type Narration } from "./narration";
import { scenarioText, type ScenarioText } from "./scenarios";

const STORAGE_KEY = "aerolab.lang";

function detectInitialLanguage(): Lang {
  try {
    const stored = window.localStorage.getItem(STORAGE_KEY);
    if (stored === "fr" || stored === "en") return stored;
  } catch {
    // localStorage can be unavailable in a hardened browser profile; the
    // interface must still work, it simply will not remember the choice.
  }
  const preferred = typeof navigator !== "undefined" ? navigator.language : "en";
  return preferred.toLowerCase().startsWith("fr") ? "fr" : "en";
}

interface LanguageContextValue {
  lang: Lang;
  setLang: (lang: Lang) => void;
  t: Strings;
  failures: FailureEntry[];
  guide: GuideStrings;
  narration: Narration;
  /** French catalogue text for a scenario, or null to use the file's English. */
  scenarioText: (id: string) => ScenarioText | null;
  /** Locale-aware number formatting: French uses a comma as decimal separator. */
  num: (value: number, digits?: number) => string;
}

const LanguageContext = createContext<LanguageContextValue | null>(null);

export function LanguageProvider({ children }: { children: ReactNode }) {
  const [lang, setLangState] = useState<Lang>(detectInitialLanguage);

  useEffect(() => {
    document.documentElement.lang = lang;
  }, [lang]);

  const setLang = useCallback((next: Lang) => {
    setLangState(next);
    try {
      window.localStorage.setItem(STORAGE_KEY, next);
    } catch {
      /* not fatal */
    }
  }, []);

  const value = useMemo<LanguageContextValue>(() => {
    const formatter = new Intl.NumberFormat(lang === "fr" ? "fr-FR" : "en-GB");
    return {
      lang,
      setLang,
      t: STRINGS[lang],
      failures: FAILURE_ENTRIES[lang],
      guide: GUIDE[lang],
      narration: NARRATION[lang],
      scenarioText: (id: string) => scenarioText(lang, id),
      num: (v: number, digits = 2) => {
        if (!Number.isFinite(v)) return "—";
        return new Intl.NumberFormat(lang === "fr" ? "fr-FR" : "en-GB", {
          minimumFractionDigits: digits,
          maximumFractionDigits: digits,
        }).format(v) || formatter.format(v);
      },
    };
  }, [lang, setLang]);

  return <LanguageContext.Provider value={value}>{children}</LanguageContext.Provider>;
}

export function useLang(): LanguageContextValue {
  const ctx = useContext(LanguageContext);
  if (!ctx) throw new Error("useLang must be used inside a LanguageProvider");
  return ctx;
}

export type { Lang, Strings, FailureEntry, GuideStrings, Narration, ScenarioText };
