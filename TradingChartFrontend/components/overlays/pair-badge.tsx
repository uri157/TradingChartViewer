"use client"

import { Badge } from "@/components/ui/badge"
import { cn } from "@/lib/utils"
import type { Symbol, Interval } from "@/lib/api/types"

interface PairBadgeProps {
  symbol: Symbol
  interval: Interval
  className?: string
}

export function PairBadge({ symbol, interval, className }: PairBadgeProps) {
  return (
    <div className={cn("absolute top-4 left-4 z-10 flex items-center gap-2", className)}>
      <Badge variant="secondary" className="bg-secondary/80 backdrop-blur-sm text-secondary-foreground font-semibold">
        {symbol}
      </Badge>
      <Badge variant="outline" className="bg-background/80 backdrop-blur-sm border-border text-foreground">
        {interval}
      </Badge>
    </div>
  )
}
