#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { S3Stack } from '../lib/stacks/s3-stack';
import { IamStack } from '../lib/stacks/iam-stack';
import { LambdaStack } from '../lib/stacks/lambda-stack';
import { EventBridgeStack } from '../lib/stacks/event-bridge-stack';
import { DynamoDBStack } from '../lib/stacks/dynamodb-stack';

const app = new cdk.App();

// Get context values from CLI or environment
const environment = app.node.tryGetContext('environment') || process.env.ENVIRONMENT || 'dev';
const fredApiKey = app.node.tryGetContext('fredApiKey') || process.env.FRED_API_KEY || '';
const alphaVantageApiKey = app.node.tryGetContext('alphaVantageApiKey') || process.env.ALPHA_VANTAGE_API_KEY || '';
const awsRegion = app.node.tryGetContext('region') || process.env.AWS_REGION || 'us-east-1';

// Validate required values
if (!fredApiKey) {
  throw new Error('FRED API key is required. Provide via --context fredApiKey=xxx or FRED_API_KEY env var');
}
if (!alphaVantageApiKey) {
  throw new Error('Alpha Vantage API key is required. Provide via --context alphaVantageApiKey=xxx or ALPHA_VANTAGE_API_KEY env var');
}

// Stack prefix for consistency
const stackPrefix = `InvertedYieldTrader${environment.charAt(0).toUpperCase() + environment.slice(1)}`;

// Create S3 stack
const s3Stack = new S3Stack(app, `${stackPrefix}S3Stack`, {
  environment,
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: awsRegion,
  },
});

// Create IAM stack (depends on S3 bucket name)
const iamStack = new IamStack(app, `${stackPrefix}IamStack`, {
  environment,
  s3BucketName: s3Stack.bucket.bucketName,
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: awsRegion,
  },
});

// Create Lambda stack (depends on IAM role and S3 bucket name)
const lambdaStack = new LambdaStack(app, `${stackPrefix}LambdaStack`, {
  environment,
  s3BucketName: s3Stack.bucket.bucketName,
  lambdaRole: iamStack.lambdaRole,
  fredApiKey,
  alphaVantageApiKey,
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: awsRegion,
  },
});

// Create DynamoDB stack for tiered storage
new DynamoDBStack(app, `${stackPrefix}DynamoDBStack`, {
  environment,
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: awsRegion,
  },
});

// Create EventBridge stack (depends on Lambda function)
new EventBridgeStack(app, `${stackPrefix}EventBridgeStack`, {
  environment,
  lambdaFunction: lambdaStack.lambdaFunction,
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: awsRegion,
  },
});
