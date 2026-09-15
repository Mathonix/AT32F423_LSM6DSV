export const designTokens = {
  radius: {
    sm: "6px",
    md: "8px",
    lg: "10px",
  },
  controlHeight: {
    compact: "28px",
    default: "32px",
    comfortable: "36px",
  },
  spacing: {
    xs: "4px",
    sm: "8px",
    md: "12px",
    lg: "16px",
    xl: "24px",
  },
  typography: {
    ui: "14px",
    meta: "12px",
    title: "18px",
    mono: "12px",
  },
  motion: {
    fast: 0.12,
    default: 0.18,
    deliberate: 0.24,
  },
} as const;

export const semanticStatus = {
  accent: "cyan / blue-green",
  success: "green",
  warning: "orange",
  error: "red",
  info: "cool blue",
} as const;
