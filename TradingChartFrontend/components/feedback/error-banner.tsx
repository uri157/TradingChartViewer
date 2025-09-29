"use client"

import { AlertCircle, X } from "lucide-react"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

interface ErrorBannerProps {
  error: string
  onDismiss?: () => void
  onRetry?: () => void
  className?: string
}

export function ErrorBanner({ error, onDismiss, onRetry, className }: ErrorBannerProps) {
  return (
    <Alert variant="destructive" className={cn("relative", className)}>
      <AlertCircle className="h-4 w-4" />
      <AlertDescription className="pr-8">
        {error}
        {onRetry && (
          <Button variant="outline" size="sm" onClick={onRetry} className="ml-2 bg-transparent">
            Retry
          </Button>
        )}
      </AlertDescription>
      {onDismiss && (
        <Button variant="ghost" size="sm" onClick={onDismiss} className="absolute right-2 top-2 h-6 w-6 p-0">
          <X className="h-3 w-3" />
        </Button>
      )}
    </Alert>
  )
}
