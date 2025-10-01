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

  
}
