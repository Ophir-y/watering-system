export type ThemeChoice = 'auto' | 'light' | 'dark';

const KEY = 'plantcare.theme';

export function getThemeChoice(): ThemeChoice {
  try {
    const v = localStorage.getItem(KEY);
    return v === 'light' || v === 'dark' ? v : 'auto';
  } catch {
    return 'auto';
  }
}

export function applyTheme(choice: ThemeChoice) {
  if (choice === 'auto') delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = choice;
}

export function setThemeChoice(choice: ThemeChoice) {
  try {
    if (choice === 'auto') localStorage.removeItem(KEY);
    else localStorage.setItem(KEY, choice);
  } catch {
    /* not persisted; still applied for this visit */
  }
  applyTheme(choice);
}
