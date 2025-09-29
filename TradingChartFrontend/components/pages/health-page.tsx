"use client"

import { ArrowLeft, Activity, Database, Clock, CheckCircle } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import { useHealth } from "@/lib/api/queries"
import { LoadingState } from "@/components/feedback/loading-state"
import { ErrorBanner } from "@/components/feedback/error-banner"
import Link from "next/link"
import { getApiDisplayBase } from "@/lib/apiClient"

export function HealthPage() {
  const { data: health, isLoading, error, refetch } = useHealth()
  const apiDisplayBase = getApiDisplayBase()

  const formatUptime = (seconds: number) => {
    const hours = Math.floor(seconds / 3600)
    const minutes = Math.floor((seconds % 3600) / 60)
    const secs = seconds % 60

    if (hours > 0) {
      return `${hours}h ${minutes}m ${secs}s`
    }
    if (minutes > 0) {
      return `${minutes}m ${secs}s`
    }
    return `${secs}s`
  }

  return (
    <div className="min-h-screen bg-background">
      {/* Header */}
      <div className="border-b border-border bg-card">
        <div className="flex items-center gap-4 p-4">
          <Button variant="ghost" size="sm" asChild>
            <Link href="/">
              <ArrowLeft className="h-4 w-4 mr-2" />
              Back to Chart
            </Link>
          </Button>
          <h1 className="text-xl font-semibold">System Health</h1>
        </div>
      </div>

      {/* Content */}
      <div className="container max-w-4xl mx-auto p-6 space-y-6">
        {error && <ErrorBanner error="Failed to load health data" onRetry={() => refetch()} />}

        {isLoading ? (
          <Card>
            <CardContent className="p-6">
              <LoadingState message="Loading system health..." />
            </CardContent>
          </Card>
        ) : (
          health && (
            <>
              {/* Overall Status */}
              <Card>
                <CardHeader>
                  <CardTitle className="flex items-center gap-2">
                    <Activity className="h-5 w-5" />
                    System Status
                  </CardTitle>
                  <CardDescription>Overall system health and performance</CardDescription>
                </CardHeader>
                <CardContent>
                  <div className="flex items-center gap-2">
                    <CheckCircle className="h-5 w-5 text-success" />
                    <Badge variant="default" className="bg-success text-success-foreground">
                      {health.status.toUpperCase()}
                    </Badge>
                    <span className="text-muted-foreground">All systems operational</span>
                  </div>
                </CardContent>
              </Card>

              {/* Services */}
              <div className="grid gap-6 md:grid-cols-2">
                {/* Database */}
                <Card>
                  <CardHeader>
                    <CardTitle className="flex items-center gap-2">
                      <Database className="h-5 w-5" />
                      Database
                    </CardTitle>
                    <CardDescription>Mock database connection status</CardDescription>
                  </CardHeader>
                  <CardContent>
                    <div className="flex items-center gap-2">
                      <CheckCircle className="h-4 w-4 text-success" />
                      <Badge variant="default" className="bg-success text-success-foreground">
                        {health.duckdb.toUpperCase()}
                      </Badge>
                    </div>
                    <div className="mt-2 text-sm text-muted-foreground">Connection established and responsive</div>
                  </CardContent>
                </Card>

                {/* Uptime */}
                <Card>
                  <CardHeader>
                    <CardTitle className="flex items-center gap-2">
                      <Clock className="h-5 w-5" />
                      Uptime
                    </CardTitle>
                    <CardDescription>System uptime and availability</CardDescription>
                  </CardHeader>
                  <CardContent>
                    <div className="text-2xl font-mono font-semibold">{formatUptime(health.uptimeSec)}</div>
                    <div className="mt-1 text-sm text-muted-foreground">System has been running continuously</div>
                  </CardContent>
                </Card>
              </div>

              {/* API Endpoints */}
              <Card>
                <CardHeader>
                  <CardTitle>API Endpoints</CardTitle>
                  <CardDescription>Status of available API endpoints</CardDescription>
                </CardHeader>
                <CardContent>
                  <div className="space-y-3">
                    {[
                      {
                        endpoint: "/api/v1/symbols",
                        description: "Trading symbol list",
                      },
                      {
                        endpoint: "/api/v1/intervals",
                        description: "Available intervals per symbol",
                      },
                      {
                        endpoint: "/api/v1/candles",
                        description: "Historical candle data",
                      },
                      {
                        endpoint: "/api/healthz",
                        description: "System health check",
                      },
                    ].map((item) => (
                      <div key={item.endpoint} className="flex items-center justify-between">
                        <div>
                          <div className="font-mono text-sm">{`${apiDisplayBase}${item.endpoint}`}</div>
                          <div className="text-xs text-muted-foreground">{item.description}</div>
                        </div>
                        <div className="flex items-center gap-2">
                          <CheckCircle className="h-4 w-4 text-success" />
                          <Badge variant="outline" className="text-success border-success">
                            Healthy
                          </Badge>
                        </div>
                      </div>
                    ))}
                  </div>
                </CardContent>
              </Card>

            </>
          )
        )}
      </div>
    </div>
  )
}
