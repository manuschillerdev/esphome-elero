import { type ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'

function TooltipProvider({ children }: { children: ComponentChildren }) {
  return <>{children}</>
}

function Tooltip({ children }: { children: ComponentChildren }) {
  return <span className="relative inline-flex group/tooltip">{children}</span>
}

/**
 * TooltipTrigger renders its child directly without a wrapper element.
 * The child (e.g. Button) is already focusable, so no extra tab stop is needed.
 * Tooltip visibility is driven by group-hover/group-focus-visible on the parent.
 */
function TooltipTrigger({ children }: { children: ComponentChildren }) {
  return <>{children}</>
}

function TooltipContent({
  className,
  children,
}: {
  className?: string
  children: ComponentChildren
  sideOffset?: number
  side?: string
}) {
  return (
    <span
      role="tooltip"
      className={cn(
        'pointer-events-none absolute bottom-full left-1/2 -translate-x-1/2 mb-2 z-50 rounded-md bg-foreground text-background px-3 py-1.5 text-xs whitespace-nowrap opacity-0 transition-opacity group-hover/tooltip:opacity-100 group-focus-visible/tooltip:opacity-100',
        className
      )}
    >
      {children}
    </span>
  )
}

export { Tooltip, TooltipTrigger, TooltipContent, TooltipProvider }
