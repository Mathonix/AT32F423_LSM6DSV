import { forwardRef, type ButtonHTMLAttributes } from "react";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const buttonVariants = cva(
  "inline-flex h-8 select-none items-center justify-center gap-1.5 whitespace-nowrap rounded-md border text-[13px] font-medium outline-none transition-[background-color,border-color,color,box-shadow] duration-150 focus-visible:ring-2 focus-visible:ring-ring/45 disabled:pointer-events-none disabled:opacity-45",
  {
    variants: {
      variant: {
        default:
          "border-accent/55 bg-accent text-accent-foreground shadow-[0_1px_0_rgba(255,255,255,0.08)_inset] hover:bg-accent-hover",
        secondary:
          "border-border bg-surface-2 text-foreground hover:border-border-strong hover:bg-surface-3",
        ghost:
          "border-transparent bg-transparent text-muted-foreground hover:bg-surface-2 hover:text-foreground",
        destructive:
          "border-danger/50 bg-danger/12 text-danger hover:bg-danger/18",
      },
      size: {
        sm: "h-7 px-2.5 text-xs",
        default: "h-8 px-3",
        lg: "h-9 px-3.5",
        icon: "h-8 w-8 px-0",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  },
);

export interface ButtonProps
  extends ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {}

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, ...props }, ref) => (
    <button
      ref={ref}
      className={cn(buttonVariants({ variant, size }), className)}
      {...props}
    />
  ),
);
Button.displayName = "Button";
