"use client"

import { Badge } from "@/components/ui/badge"
import { cn } from "@/lib/utils"

interface PriceScaleProps {
  currentPrice: number | null
  symbol: string
  className?: string
}

export function PriceScale({ currentPrice, symbol, className }: PriceScaleProps) {
  const formatPrice = (price: number) => {
    if (price < 1) {
      return price.toFixed(4)
    }
    if (price < 100) {
      return price.toFixed(2)
    }
    return price.toLocaleString(undefined, {
      minimumFractionDigits: 2,
      maximumFractionDigits: 2,
    })
  }

  return (
    <div className={cn("flex flex-col items-end justify-center bg-card border-l border-border", className)}>
      {currentPrice && (
        <div className="relative">
          <Badge variant="default" className="bg-primary text-primary-foreground font-mono text-sm px-2 py-1">
            ${formatPrice(currentPrice)}
          </Badge>
          <div className="absolute left-0 top-1/2 -translate-y-1/2 -translate-x-full w-2 h-px bg-primary" />
        </div>
      )}
    </div>
  )
}
