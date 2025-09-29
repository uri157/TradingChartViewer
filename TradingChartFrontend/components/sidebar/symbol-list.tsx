"use client"

import { useState } from "react"
import { Search } from "lucide-react"
import { Input } from "@/components/ui/input"
import { ScrollArea } from "@/components/ui/scroll-area"
import { useSymbols } from "@/lib/api/queries"
import { useChartStore } from "@/lib/state/use-chart-store"
import { SymbolListItem } from "./symbol-list-item"
import { LoadingState } from "@/components/feedback/loading-state"
import { ErrorBanner } from "@/components/feedback/error-banner"
import { cn } from "@/lib/utils"

interface SymbolListProps {
  className?: string
}

export function SymbolList({ className }: SymbolListProps) {
  const [searchQuery, setSearchQuery] = useState("")
  const { data: symbols, isLoading, error, refetch } = useSymbols()
  const { symbol: selectedSymbol, setSymbol } = useChartStore()

  const filteredSymbols = symbols?.filter((symbolMeta) => {
    const haystack = [symbolMeta.symbol, symbolMeta.base, symbolMeta.quote]
      .filter(Boolean)
      .join(" ")
      .toLowerCase()
    return haystack.includes(searchQuery.toLowerCase())
  })

  if (error) {
    return (
      <div className={cn("p-4", className)}>
        <ErrorBanner error="Failed to load symbols" onRetry={() => refetch()} />
      </div>
    )
  }

  return (
    <div className={cn("flex flex-col h-full bg-sidebar border-l border-sidebar-border", className)}>
      {/* Search Header */}
      <div className="p-4 border-b border-sidebar-border">
        <div className="relative">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 h-4 w-4 text-muted-foreground" />
          <Input
            placeholder="Search symbols..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="pl-9 bg-sidebar-accent border-sidebar-border text-sidebar-foreground placeholder:text-muted-foreground"
          />
        </div>
      </div>

      {/* Symbol List */}
      <ScrollArea className="flex-1">
        {isLoading ? (
          <LoadingState message="Loading symbols..." className="p-4" />
        ) : (
          <div className="p-2">
            {filteredSymbols?.map((symbolMeta) => (
              <SymbolListItem
                key={symbolMeta.symbol}
                symbol={symbolMeta}
                isSelected={symbolMeta.symbol === selectedSymbol}
                onClick={() => setSymbol(symbolMeta.symbol)}
              />
            ))}
            {filteredSymbols?.length === 0 && searchQuery && (
              <div className="p-4 text-center text-muted-foreground text-sm">No symbols found for "{searchQuery}"</div>
            )}
          </div>
        )}
      </ScrollArea>
    </div>
  )
}
