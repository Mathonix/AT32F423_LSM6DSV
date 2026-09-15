import * as echarts from "echarts";
import { useEffect, useRef } from "react";
import { useTheme } from "@/components/theme/theme-provider";

export function EChart({ option, className = "h-56 w-full" }: { option: echarts.EChartsOption; className?: string }) {
  const ref = useRef<HTMLDivElement>(null);
  const instance = useRef<echarts.ECharts | null>(null);
  const { theme } = useTheme();

  useEffect(() => {
    if (!ref.current) return;
    const chart = echarts.init(ref.current, undefined, { renderer: "canvas" });
    instance.current = chart;
    const resize = new ResizeObserver(() => chart.resize());
    resize.observe(ref.current);
    return () => {
      resize.disconnect();
      chart.dispose();
      instance.current = null;
    };
  }, []);

  useEffect(() => {
    const chart = instance.current;
    if (!chart) return;
    const style = getComputedStyle(document.documentElement);
    const foreground = style.getPropertyValue("--foreground").trim();
    const muted = style.getPropertyValue("--muted-foreground").trim();
    const border = style.getPropertyValue("--border").trim();
    chart.setOption(
      {
        backgroundColor: "transparent",
        textStyle: { color: foreground, fontFamily: "Inter, Segoe UI, sans-serif" },
        grid: { left: 46, right: 16, top: 22, bottom: 28, containLabel: false },
        tooltip: { trigger: "axis", backgroundColor: style.getPropertyValue("--surface-2").trim(), borderColor: border, textStyle: { color: foreground, fontSize: 11 } },
        xAxis: { axisLine: { lineStyle: { color: border } }, axisTick: { show: false }, axisLabel: { color: muted, fontSize: 10 }, splitLine: { show: false } },
        yAxis: { axisLine: { show: false }, axisTick: { show: false }, axisLabel: { color: muted, fontSize: 10 }, splitLine: { lineStyle: { color: border, opacity: 0.6 } } },
        animationDuration: 180,
        ...option,
      },
      { notMerge: true },
    );
  }, [option, theme]);

  return <div ref={ref} className={className} />;
}
