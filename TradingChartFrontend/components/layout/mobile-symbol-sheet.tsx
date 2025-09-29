"use client"

import { Menu } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Sheet, SheetContent, SheetHeader, SheetTitle, SheetTrigger } from "@/components/ui/sheet"
import { SymbolList } from "@/components/sidebar/symbol-list"
import { useMobile } from "@/hooks/use-mobile"

export function MobileSymbolSheet() {
  const isMobile = useMobile()

  if (!isMobile) return null

  return (
    <Sheet>
      <SheetTrigger asChild>
        <Button variant="ghost" size="sm">
          <Menu className="h-4 w-4" />
        </Button>
      </SheetTrigger>
      <SheetContent side="right" className="w-80 p-0">
        <SheetHeader className="p-4 border-b">
          <SheetTitle>Trading Symbols</SheetTitle>
        </SheetHeader>
        <SymbolList className="border-0" />
      </SheetContent>
    </Sheet>
  )
}
