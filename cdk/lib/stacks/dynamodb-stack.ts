import * as cdk from 'aws-cdk-lib';
import * as dynamodb from 'aws-cdk-lib/aws-dynamodb';
import { Construct } from 'constructs';

interface DynamoDBStackProps extends cdk.StackProps {
  environment: string;
}

export class DynamoDBStack extends cdk.Stack {
  public readonly signalsTable: dynamodb.Table;
  public readonly backtestTable: dynamodb.Table;

  constructor(scope: Construct, id: string, props: DynamoDBStackProps) {
    super(scope, id, props);

    // Hot data table: recent signals and positions (last 30 days)
    // Partition key: date (YYYY-MM-DD), Sort key: indicator
    this.signalsTable = new dynamodb.Table(this, 'MacroSignalsTable', {
      tableName: `macro-signals-${props.environment}`,
      partitionKey: {
        name: 'date',
        type: dynamodb.AttributeType.STRING,
      },
      sortKey: {
        name: 'indicator',
        type: dynamodb.AttributeType.STRING,
      },
      billingMode: dynamodb.BillingMode.PAY_PER_REQUEST,
      removalPolicy: cdk.RemovalPolicy.RETAIN,
      pointInTimeRecoveryEnabled: true,
      timeToLiveAttribute: 'ttl',
    });

    // Backtest results table: historical simulation outputs
    // Partition key: backtest_id, Sort key: date
    this.backtestTable = new dynamodb.Table(this, 'BacktestResultsTable', {
      tableName: `backtest-results-${props.environment}`,
      partitionKey: {
        name: 'backtest_id',
        type: dynamodb.AttributeType.STRING,
      },
      sortKey: {
        name: 'date',
        type: dynamodb.AttributeType.STRING,
      },
      billingMode: dynamodb.BillingMode.PAY_PER_REQUEST,
      removalPolicy: cdk.RemovalPolicy.RETAIN,
      pointInTimeRecoveryEnabled: true,
    });

    // GSI for querying signals by indicator across dates
    this.signalsTable.addGlobalSecondaryIndex({
      indexName: 'indicator-date-index',
      partitionKey: {
        name: 'indicator',
        type: dynamodb.AttributeType.STRING,
      },
      sortKey: {
        name: 'date',
        type: dynamodb.AttributeType.STRING,
      },
      projectionType: dynamodb.ProjectionType.ALL,
    });

    // Outputs
    new cdk.CfnOutput(this, 'SignalsTableName', {
      value: this.signalsTable.tableName,
      exportName: `${id}-SignalsTableName`,
    });

    new cdk.CfnOutput(this, 'BacktestTableName', {
      value: this.backtestTable.tableName,
      exportName: `${id}-BacktestTableName`,
    });
  }
}
