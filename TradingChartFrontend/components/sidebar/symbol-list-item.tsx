"use client"

import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { cn } from "@/lib/utils"
import type { SymbolsResponse } from "@/lib/api/types"

interface SymbolListItemProps {
  symbol: SymbolsResponse["symbols"][number]
  isSelected: boolean
  onClick: () => void
}

export function SymbolListItem({ symbol, isSelected, onClick }: SymbolListItemProps) {
  const description = [symbol.base, symbol.quote].filter(Boolean).join(" / ")
  const statusLabel = symbol.status ? symbol.status.toUpperCase() : null
  const statusVariant = symbol.status === "inactive" ? "destructive" : "outline"

  return (
    <Button
      variant={isSelected ? "secondary" : "ghost"}
      onClick={onClick}
      className={cn(
        "w-full justify-between p-3 h-auto text-left hover:bg-sidebar-accent",
        isSelected && "bg-sidebar-accent border border-sidebar-border",
      )}
    >
      <div className="flex flex-col items-start gap-1">
        <div className="font-semibold text-sidebar-foreground">{symbol.symbol}</div>
        {description && (
          <div className="text-[11px] uppercase tracking-wide text-muted-foreground">{description}</div>
        )}
      </div>

      {statusLabel && (
        <Badge variant={statusVariant} className="text-[10px] tracking-wide uppercase">
          {statusLabel}
        </Badge>
      )}
    </Button>
  )
}
